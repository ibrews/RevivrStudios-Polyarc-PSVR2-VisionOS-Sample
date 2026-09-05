#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "IHandTracker.h"
#include "XRMotionControllerBase.h"
#include "Misc/ScopeLock.h"
#include "MotionControllerComponent.h"
#include "UnrealClient.h"
#include "HeadMountedDisplayTypes.h"
#include "InputCoreTypes.h"
#include "IXRTrackingSystem.h"
#include "IHeadMountedDisplay.h"
#include "Features/IModularFeatures.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "PlayInEditorDataTypes.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UnrealType.h"
#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "XrsimPresentation.h"

namespace
{
const TCHAR *Protocol = TEXT("xrsim.hand.v1");
using Json = TSharedPtr<FJsonObject>;
Json Obj()
{
    return MakeShared<FJsonObject>();
}
TArray<TSharedPtr<FJsonValue>> V3(const FVector &V)
{
    return {MakeShared<FJsonValueNumber>(V.Y / 100), MakeShared<FJsonValueNumber>(V.Z / 100),
            MakeShared<FJsonValueNumber>(-V.X / 100)};
}
FVector FromXR(const TArray<TSharedPtr<FJsonValue>> &A)
{
    return FVector(-A[2]->AsNumber(), A[0]->AsNumber(), A[1]->AsNumber()) * 100;
}
bool BoolProp(UObject *O, const TCHAR *Name)
{
    const FBoolProperty *P = FindFProperty<FBoolProperty>(O->GetClass(), Name);
    return P && P->GetPropertyValue_InContainer(O);
}
bool Grabbable(AActor *A)
{
    for (UActorComponent *C : A->GetComponents())
        if (C && C->GetClass()->GetName().Contains(TEXT("Grab")))
            return true;
    return false;
}
FVector Velocity(AActor *A)
{
    for (UActorComponent *C : A->GetComponents())
        if (UPrimitiveComponent *P = Cast<UPrimitiveComponent>(C))
            if (P->IsSimulatingPhysics())
                return P->GetPhysicsLinearVelocity();
    return FVector::ZeroVector;
}
bool Vec(const Json &O, const TCHAR *K, int32 Count, TArray<TSharedPtr<FJsonValue>> &Out)
{
    const TArray<TSharedPtr<FJsonValue>> *A;
    if (!O->TryGetArrayField(K, A) || A->Num() != Count)
        return false;
    for (auto &V : *A)
    {
        double N;
        if (!V->TryGetNumber(N) || !FMath::IsFinite(N))
            return false;
    }
    Out = *A;
    return true;
}
struct Hand
{
    bool Tracked = false;
    FVector Point = FVector::ZeroVector;
    FQuat Quat = FQuat::Identity;
    double Pinch = 0;
    FVector Curls = FVector::ZeroVector;
};
struct Frame
{
    double T = 0;
    Hand Hands[2];
};
} // namespace

// No gameplay setters live in this module: input is IHandTracker data, observations are actor
// attachment/physics and reflected hand state. The application owns grab/release/throw logic.
class FXrsimAgent : public IModuleInterface, public IHandTracker, public FXRMotionControllerBase
{
    FString Dir, RunId, PreviewId, Instance;
    TArray<Frame> Frames;
    Hand Hands[2];
    TWeakObjectPtr<AActor> Target;
    TArray<TSharedPtr<FJsonValue>> Trace;
    double Elapsed = 0, LastState = 0, PreviewStart = 0;
    double RunStart = 0, PreviousSampleTime = -1;
    Json RunScenario;
    bool HeldShot = false, FlightShot = false, Capture = false;
    FVector GrabPos, ReleasePos;
    FVector IntentDirection = FVector::ZeroVector, ReleaseDirection = FVector::ZeroVector;
    FVector LastWorldPoints[2], WorldVelocity[2];
    double ForwardSpeed = 0, ForwardFlight = 0;
    double HeldDistance = 0, ReleaseSpeed = 0, FlightDistance = 0, MaxDt = 0;
    bool Grabbed = false, Released = false, PinchSeen = false, ReleaseSeen = false;
    mutable uint64 Reads = 0;
    mutable FCriticalSection MotionMutex;
    Hand MotionHands[2];
    FVector MotionVelocity[2];
    FVector MotionAimPoint[2], MotionAimVelocity[2];
    mutable uint64 MotionReads = 0;
    FDelegateHandle PreHandle, PostHandle;
    FTSTicker::FDelegateHandle TickHandle;
    FXrsimPresentation Presentation;

    UWorld *PlayWorld() const
    {
        return GEditor ? GEditor->PlayWorld : nullptr;
    }
    bool VR() const
    {
        UWorld *W = PlayWorld();
        return W && GEngine->XRSystem.IsValid() && GEngine->XRSystem->IsHeadTrackingAllowedForWorld(*W) &&
               GEngine->IsStereoscopic3D();
    }
    void Write(const FString &File, const Json &Data)
    {
        FString Text;
        FJsonSerializer::Serialize(Data.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
        const FString Path = Dir / File, Temp = Path + TEXT(".tmp");
        if (FFileHelper::SaveStringToFile(Text, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            IFileManager::Get().Move(*Path, *Temp, true, true);
    }
    void Reply(const FString &Id, Json Data)
    {
        Data->SetStringField(TEXT("id"), Id);
        Data->SetStringField(TEXT("protocol"), Protocol);
        Write(TEXT("response-") + Id + TEXT(".json"), Data);
    }
    void Error(const FString &Id, const FString &Message)
    {
        Json J = Obj();
        J->SetBoolField(TEXT("ok"), false);
        J->SetStringField(TEXT("error"), Message);
        Reply(Id, J);
    }
    Json Scene()
    {
        Json J = Obj();
        J->SetStringField(TEXT("protocol"), Protocol);
        J->SetStringField(TEXT("instance"), Instance);
        J->SetStringField(TEXT("input_source"), TEXT("unreal_ihandtracker_injection"));
        TArray<TSharedPtr<FJsonValue>> Trackers;
        for (auto *T : IModularFeatures::Get().GetModularFeatureImplementations<IHandTracker>(
                 IHandTracker::GetModularFeatureName()))
        {
            Json D = Obj();
            D->SetStringField(TEXT("name"), T->GetHandTrackerDeviceTypeName().ToString());
            D->SetBoolField(TEXT("injected"), T == this);
            Trackers.Add(MakeShared<FJsonValueObject>(D));
        }
        J->SetArrayField(TEXT("trackers"), Trackers);
        J->SetNumberField(TEXT("updated_ms"),
                          (FDateTime::UtcNow().GetTicks() - FDateTime(1970, 1, 1).GetTicks()) / 10000.0);
        J->SetNumberField(TEXT("pid"), FPlatformProcess::GetCurrentProcessId());
        J->SetStringField(TEXT("project"), FPaths::GetProjectFilePath());
        J->SetBoolField(TEXT("vr_preview"), VR());
        J->SetBoolField(TEXT("busy"), !RunId.IsEmpty());
        J->SetStringField(TEXT("run_id"), RunId);
        J->SetNumberField(TEXT("scenario_time_s"), Elapsed);
        J->SetObjectField(TEXT("view"), Presentation.State());
        J->SetNumberField(TEXT("joint_reads"), Reads);
        UWorld *W = PlayWorld();
        if (!W && GEditor)
            W = GEditor->GetEditorWorldContext().World();
        TArray<TSharedPtr<FJsonValue>> Actors, Components;
        if (W)
        {
            J->SetStringField(TEXT("world"), W->GetName());
            if (APawn *P = UGameplayStatics::GetPlayerPawn(W, 0))
            {
                J->SetStringField(TEXT("pawn"), P->GetName());
                J->SetArrayField(TEXT("pawn_pos"), V3(P->GetActorLocation()));
                J->SetBoolField(TEXT("pawn_tick_enabled"), P->IsActorTickEnabled());
                Json Props = Obj();
                for (TFieldIterator<FBoolProperty> It(P->GetClass()); It; ++It)
                    Props->SetBoolField(It->GetName(), It->GetPropertyValue_InContainer(P));
                J->SetObjectField(TEXT("pawn_bools"), Props);
            }
            for (TActorIterator<AActor> It(W); It; ++It)
            {
                AActor *A = *It;
                if (Grabbable(A))
                {
                    Json D = Obj();
                    D->SetStringField(TEXT("name"), A->GetName());
                    D->SetStringField(TEXT("label"), A->GetActorLabel());
                    D->SetStringField(TEXT("class"), A->GetClass()->GetName());
                    D->SetBoolField(TEXT("grabbable"), true);
                    D->SetArrayField(TEXT("pos"), V3(A->GetActorLocation()));
                    D->SetArrayField(TEXT("velocity"), V3(Velocity(A)));
                    D->SetBoolField(TEXT("attached"), A->GetAttachParentActor() != nullptr);
                    FVector Center, Extent;
                    A->GetActorBounds(true, Center, Extent);
                    D->SetArrayField(TEXT("bounds_center"), V3(Center));
                    D->SetArrayField(TEXT("bounds_extent"), V3(Extent * FVector(-1, 1, 1)));
                    Actors.Add(MakeShared<FJsonValueObject>(D));
                }
                for (UActorComponent *C : A->GetComponents())
                    if (C && C->GetClass()->GetName().Contains(TEXT("HandTrackingComponent")))
                    {
                        Json D = Obj();
                        D->SetStringField(TEXT("owner"), A->GetName());
                        D->SetBoolField(TEXT("right"), BoolProp(C, TEXT("bIsRight")));
                        D->SetBoolField(TEXT("tracking"), BoolProp(C, TEXT("bIsTracking")));
                        D->SetBoolField(TEXT("pinching"), BoolProp(C, TEXT("bIsPinching")));
                        Components.Add(MakeShared<FJsonValueObject>(D));
                    }
            }
        }
        J->SetArrayField(TEXT("actors"), Actors);
        J->SetArrayField(TEXT("hand_components"), Components);
        return J;
    }
    bool ParseFrames(const Json &S, FString &Why)
    {
        Frames.Reset();
        const TArray<TSharedPtr<FJsonValue>> *F;
        if (!S.IsValid() || S->GetIntegerField(TEXT("version")) != 1 ||
            !S->TryGetArrayField(TEXT("frames"), F) || F->Num() < 2 || F->Num() > 512)
        {
            Why = TEXT("invalid scenario version/frames");
            return false;
        }
        double Last = -1;
        for (auto &Value : *F)
        {
            Json J = Value->AsObject();
            Frame N;
            const TSharedPtr<FJsonObject> *H;
            if (!J || !J->TryGetNumberField(TEXT("t"), N.T) || !FMath::IsFinite(N.T) || N.T <= Last ||
                N.T > 30 || (Frames.IsEmpty() && N.T != 0) || !J->TryGetObjectField(TEXT("hands"), H))
            {
                Why = TEXT("invalid frame timing/hands");
                return false;
            }
            Last = N.T;
            for (int Side = 0; Side < 2; ++Side)
            {
                const TSharedPtr<FJsonObject> *A;
                if ((*H)->TryGetObjectField(Side ? TEXT("right") : TEXT("left"), A))
                {
                    TArray<TSharedPtr<FJsonValue>> P, Q;
                    if (!Vec(*A, TEXT("pinchPoint"), 3, P) ||
                        !(*A)->TryGetNumberField(TEXT("pinch"), N.Hands[Side].Pinch) ||
                        !FMath::IsFinite(N.Hands[Side].Pinch) || N.Hands[Side].Pinch < 0 ||
                        N.Hands[Side].Pinch > 1)
                    {
                        Why = TEXT("invalid pinchPoint/pinch");
                        return false;
                    }
                    N.Hands[Side].Tracked = true;
                    N.Hands[Side].Point = FromXR(P);
                    if ((*A)->HasField(TEXT("curls")))
                    {
                        TArray<TSharedPtr<FJsonValue>> C;
                        if (!Vec(*A, TEXT("curls"), 3, C))
                        {
                            Why = TEXT("invalid curls");
                            return false;
                        }
                        for (int I = 0; I < 3; ++I)
                        {
                            double V = C[I]->AsNumber();
                            if (V < 0 || V > 1)
                            {
                                Why = TEXT("curls must be within 0..1");
                                return false;
                            }
                            N.Hands[Side].Curls[I] = V;
                        }
                    }
                    if ((*A)->HasField(TEXT("quat")))
                    {
                        if (!Vec(*A, TEXT("quat"), 4, Q))
                        {
                            Why = TEXT("invalid quaternion");
                            return false;
                        }
                        N.Hands[Side].Quat =
                            FQuat(Q[2]->AsNumber(), -Q[0]->AsNumber(), -Q[1]->AsNumber(), Q[3]->AsNumber());
                        if (FMath::Abs(N.Hands[Side].Quat.Size() - 1) > 0.001)
                        {
                            Why = TEXT("quaternion must be normalized");
                            return false;
                        }
                    }
                }
                if (!Frames.IsEmpty() && N.Hands[Side].Tracked != Frames[0].Hands[Side].Tracked)
                {
                    Why = TEXT("every frame must specify the same hands");
                    return false;
                }
            }
            if (!N.Hands[0].Tracked && !N.Hands[1].Tracked)
            {
                Why = TEXT("no hands in frame");
                return false;
            }
            Frames.Add(N);
        }
        return true;
    }
    void Request(const Json &J)
    {
        FString Id, Op, P;
        if (!J->TryGetStringField(TEXT("id"), Id) || !J->TryGetStringField(TEXT("op"), Op) ||
            !J->TryGetStringField(TEXT("protocol"), P))
            return;
        FGuid Guid;
        if (!FGuid::Parse(Id, Guid))
            return; // response filenames must never be supplied paths
        if (P != Protocol)
        {
            Error(Id, TEXT("unsupported protocol"));
            return;
        }
        double Issued;
        FString RequestInstance;
        const double Now = (FDateTime::UtcNow().GetTicks() - FDateTime(1970, 1, 1).GetTicks()) / 10000.0;
        if (!J->TryGetNumberField(TEXT("issued_ms"), Issued) || !FMath::IsFinite(Issued) ||
            FMath::Abs(Now - Issued) > 10000 || !J->TryGetStringField(TEXT("instance"), RequestInstance) ||
            RequestInstance != Instance)
        {
            Error(Id, TEXT("stale request or editor instance changed"));
            return;
        }
        if (Op == TEXT("inspect"))
        {
            Json R = Scene();
            TArray<TSharedPtr<FJsonValue>> GrabComponents;
            if (PlayWorld())
                for (TActorIterator<AActor> A(PlayWorld()); A; ++A)
                    for (UActorComponent *C : A->GetComponents())
                        if (C->GetClass()->GetName().Contains(TEXT("Grab")))
                        {
                            Json D = Obj(), Parameters = Obj();
                            D->SetStringField(TEXT("actor"), A->GetName());
                            D->SetStringField(TEXT("component"), C->GetClass()->GetPathName());
                            for (TFieldIterator<FNumericProperty> Property(C->GetClass()); Property; ++Property)
                                if (Property->IsFloatingPoint())
                                    Parameters->SetNumberField(Property->GetName(), Property->GetFloatingPointPropertyValue(Property->ContainerPtrToValuePtr<void>(C)));
                            D->SetObjectField(TEXT("parameters"), Parameters);
                            GrabComponents.Add(MakeShared<FJsonValueObject>(D));
                        }
            R->SetArrayField(TEXT("grab_components"), GrabComponents);
            TArray<TSharedPtr<FJsonValue>> Nodes;
            if (APawn *Pawn = UGameplayStatics::GetPlayerPawn(PlayWorld(), 0))
                if (UBlueprint *BP = Cast<UBlueprint>(Pawn->GetClass()->ClassGeneratedBy))
                {
                    TArray<UEdGraph *> Graphs;
                    BP->GetAllGraphs(Graphs);
                    for (UEdGraph *G : Graphs)
                        for (UEdGraphNode *N : G->Nodes)
                        {
                            Json D = Obj();
                            D->SetStringField(TEXT("graph"), G->GetName());
                            D->SetStringField(TEXT("node"), N->GetName());
                            D->SetStringField(TEXT("title"),
                                              N->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
                            TArray<TSharedPtr<FJsonValue>> Pins;
                            for (UEdGraphPin *Pin : N->Pins)
                            {
                                Json Q = Obj();
                                Q->SetStringField(TEXT("name"), Pin->PinName.ToString());
                                Q->SetStringField(TEXT("default"), Pin->DefaultValue);
                                Q->SetStringField(TEXT("direction"),
                                                  Pin->Direction == EGPD_Input ? TEXT("in") : TEXT("out"));
                                TArray<TSharedPtr<FJsonValue>> Links;
                                for (UEdGraphPin *L : Pin->LinkedTo)
                                    Links.Add(MakeShared<FJsonValueString>(
                                        L->GetOwningNode()->GetName() + TEXT(".") + L->PinName.ToString()));
                                Q->SetArrayField(TEXT("links"), Links);
                                Pins.Add(MakeShared<FJsonValueObject>(Q));
                            }
                            D->SetArrayField(TEXT("pins"), Pins);
                            Nodes.Add(MakeShared<FJsonValueObject>(D));
                        }
                }
            R->SetArrayField(TEXT("blueprint_nodes"), Nodes);
            R->SetBoolField(TEXT("ok"), true);
            Reply(Id, R);
            return;
        }
        if (Op == TEXT("stop"))
        {
            if (!RunId.IsEmpty())
            {
                Error(RunId, TEXT("scenario stopped"));
                RunId.Empty();
            }
            Hands[0].Tracked = Hands[1].Tracked = false;
            Json R = Obj();
            R->SetBoolField(TEXT("ok"), true);
            Reply(Id, R);
            return;
        }
        if (!RunId.IsEmpty() || !PreviewId.IsEmpty())
        {
            Error(Id, TEXT("another request is active"));
            return;
        }
        if (Op == TEXT("preview"))
        {
            if (PlayWorld())
            {
                if (VR())
                {
                    Json R = Scene();
                    R->SetBoolField(TEXT("ok"), true);
                    Reply(Id, R);
                }
                else
                    Error(Id, TEXT("an existing play session is not VR Preview"));
                return;
            }
            if (!GEngine->XRSystem.IsValid() || !GEngine->XRSystem->GetHMDDevice() ||
                !GEngine->XRSystem->GetHMDDevice()->IsHMDConnected())
            {
                Error(Id, TEXT("no simulated HMD is available; launch this editor with XR_RUNTIME_JSON "
                               "pointing to Meta XR Simulator"));
                return;
            }
            FRequestPlaySessionParams Params;
            Params.SessionPreviewTypeOverride = EPlaySessionPreviewType::VRPreview;
            GEditor->RequestPlaySession(Params);
            PreviewId = Id;
            PreviewStart = FPlatformTime::Seconds();
            return;
        }
        if (Op == TEXT("view"))
        {
            const TSharedPtr<FJsonObject> *Config;
            FString Why;
            if (!VR() || !J->TryGetObjectField(TEXT("view"), Config))
                Error(Id, TEXT("view requires VR Preview and a view configuration"));
            else if (!Presentation.Configure(PlayWorld(), *Config, Why))
                Error(Id, Why);
            else
            {
                Json R = Presentation.State();
                R->SetBoolField(TEXT("ok"), true);
                Reply(Id, R);
            }
            return;
        }
        if (Op == TEXT("end-preview"))
        {
            Presentation.Reset();
            if (PlayWorld())
                GEditor->RequestEndPlayMap();
            Json R = Obj();
            R->SetBoolField(TEXT("ok"), true);
            Reply(Id, R);
            return;
        }
        if (Op != TEXT("run"))
        {
            Error(Id, TEXT("unknown operation"));
            return;
        }
        if (!VR())
        {
            Error(Id, TEXT("VR Preview is not running"));
            return;
        }
        const TSharedPtr<FJsonObject> *S;
        FString Why, Name;
        if (!J->TryGetObjectField(TEXT("scenario"), S) || !(*S)->TryGetStringField(TEXT("target"), Name) ||
            !ParseFrames(*S, Why))
        {
            Error(Id, Why.IsEmpty() ? TEXT("invalid scenario") : Why);
            return;
        }
        Target.Reset();
        for (TActorIterator<AActor> It(PlayWorld()); It; ++It)
            if (It->GetName() == Name && Grabbable(*It))
            {
                Target = *It;
                break;
            }
        if (!Target.IsValid())
        {
            Error(Id, TEXT("target absent or not grabbable"));
            return;
        }
        if (Target->GetAttachParentActor())
        {
            Error(Id, TEXT("target is already attached; choose a free object"));
            return;
        }
        // TMultiMap enumerates the most recently registered provider first. OpenXR may
        // register again at PIE startup, so select this opt-in source at dispatch too.
        auto &Features = IModularFeatures::Get();
        Features.UnregisterModularFeature(IHandTracker::GetModularFeatureName(),
                                          static_cast<IHandTracker *>(this));
        Features.RegisterModularFeature(IHandTracker::GetModularFeatureName(),
                                        static_cast<IHandTracker *>(this));
        Features.UnregisterModularFeature(IMotionController::GetModularFeatureName(),
                                          static_cast<IMotionController *>(this));
        Features.RegisterModularFeature(IMotionController::GetModularFeatureName(),
                                        static_cast<IMotionController *>(this));
        if (Features.GetModularFeatureImplementations<IHandTracker>(
                IHandTracker::GetModularFeatureName())[0] != this)
        {
            Error(Id, TEXT("injected hand provider was not selected"));
            return;
        }
        RunId = Id;
        Elapsed = 0;
        Reads = 0;
        MaxDt = 0;
        Trace.Reset();
        Grabbed = Released = PinchSeen = ReleaseSeen = false;
        HeldDistance = ReleaseSpeed = FlightDistance = 0;
        RunStart = FPlatformTime::Seconds();
        PreviousSampleTime = -1;
        RunScenario = *S;
        HeldShot = FlightShot = false;
        Capture = false;
        J->TryGetBoolField(TEXT("capture"), Capture);
        ForwardSpeed = ForwardFlight = 0;
        IntentDirection = ReleaseDirection = FVector::ZeroVector;
        {
            FScopeLock Lock(&MotionMutex);
            MotionReads = 0;
        }
    }
    bool Tick(float)
    {
        Presentation.Tick(PlayWorld());
        TArray<FString> Requests;
        IFileManager::Get().FindFiles(Requests, *(Dir / TEXT("request-*.json")), true, false);
        Requests.Sort();
        for (const FString &File : Requests)
        {
            FString Text;
            Json J;
            const FString Path = Dir / File;
            if (FFileHelper::LoadFileToString(Text, *Path))
            {
                IFileManager::Get().Delete(*Path);
                if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J) && J.IsValid())
                    Request(J);
            }
        }
        if (!PreviewId.IsEmpty())
        {
            if (VR())
            {
                Json R = Scene();
                R->SetBoolField(TEXT("ok"), true);
                Reply(PreviewId, R);
                PreviewId.Empty();
            }
            else if (FPlatformTime::Seconds() - PreviewStart > 45)
            {
                Error(PreviewId, TEXT("VR Preview did not become active within 45 seconds"));
                PreviewId.Empty();
            }
        }
        if (!RunId.IsEmpty() && !PlayWorld())
        {
            Error(RunId, TEXT("play session ended during scenario"));
            RunId.Empty();
            Hands[0].Tracked = Hands[1].Tracked = false;
        }
        if (!RunId.IsEmpty() && FPlatformTime::Seconds() - RunStart > 45)
        {
            Error(RunId, TEXT("scenario exceeded 45 seconds of wall time"));
            RunId.Empty();
            Hands[0].Tracked = Hands[1].Tracked = false;
        }
        if (RunId.IsEmpty())
        {
            FScopeLock Lock(&MotionMutex);
            MotionHands[0].Tracked = MotionHands[1].Tracked = false;
        }
        if (FPlatformTime::Seconds() - LastState > 0.1)
        {
            Write(TEXT("state.json"), Scene());
            LastState = FPlatformTime::Seconds();
        }
        return true;
    }
    void Pre(UWorld *W, ELevelTick, float Dt)
    {
        if (RunId.IsEmpty() || W != PlayWorld())
            return;
        MaxDt = FMath::Max(MaxDt, double(Dt));
        int32 B = 1;
        while (B < Frames.Num() - 1 && Frames[B].T < Elapsed)
            ++B;
        const Frame &A = Frames[B - 1];
        const Frame &Z = Frames[B];
        const double Alpha = FMath::Clamp((Elapsed - A.T) / (Z.T - A.T), 0.0, 1.0);
        for (int Side = 0; Side < 2; ++Side)
        {
            Hands[Side].Tracked = A.Hands[Side].Tracked;
            Hands[Side].Point = FMath::Lerp(A.Hands[Side].Point, Z.Hands[Side].Point, Alpha);
            Hands[Side].Pinch = FMath::Lerp(A.Hands[Side].Pinch, Z.Hands[Side].Pinch, Alpha);
            Hands[Side].Quat = FQuat::FastLerp(A.Hands[Side].Quat, Z.Hands[Side].Quat, Alpha).GetNormalized();
        }
        for (int Side = 0; Side < 2; ++Side)
            Hands[Side].Curls = FMath::Lerp(A.Hands[Side].Curls, Z.Hands[Side].Curls, Alpha);
        // Native grab anchors consume calibrated-space aim/grip poses through
        // IMotionController. These derive from the same joints, with no actor mutation.
        const FTransform TrackingToWorld = GEngine->XRSystem->GetTrackingToWorldTransform();
        FScopeLock Lock(&MotionMutex);
        const double SampleDt = Elapsed - PreviousSampleTime;
        for (int Side = 0; Side < 2; ++Side)
        {
            WorldVelocity[Side] = (PreviousSampleTime >= 0 && SampleDt > 0)
                                      ? (Hands[Side].Point - LastWorldPoints[Side]) / SampleDt
                                      : FVector::ZeroVector;
            LastWorldPoints[Side] = Hands[Side].Point;
            const FVector Aim = TrackingToWorld.InverseTransformPosition(Hands[Side].Point);
            MotionAimVelocity[Side] = (PreviousSampleTime >= 0 && MotionHands[Side].Tracked && SampleDt > 0)
                                          ? (Aim - MotionAimPoint[Side]) / SampleDt
                                          : FVector::ZeroVector;
            MotionAimPoint[Side] = Aim;
            Hand Next = Hands[Side];
            Next.Point += Next.Quat.RotateVector(FVector(-6.5, 0, -2.5));
            Next.Point = TrackingToWorld.InverseTransformPosition(Next.Point);
            Next.Quat = TrackingToWorld.InverseTransformRotation(Next.Quat);
            MotionVelocity[Side] = (PreviousSampleTime >= 0 && MotionHands[Side].Tracked && SampleDt > 0)
                                       ? (Next.Point - MotionHands[Side].Point) / SampleDt
                                       : FVector::ZeroVector;
            MotionHands[Side] = Next;
        }
        PreviousSampleTime = Elapsed;
    }
    void Post(UWorld *W, ELevelTick, float Dt)
    {
        if (W == PlayWorld())
            Presentation.UpdateHands(this);
        if (RunId.IsEmpty() || W != PlayWorld())
            return;
        if (!Target.IsValid())
        {
            Error(RunId, TEXT("target destroyed during scenario"));
            RunId.Empty();
            Hands[0].Tracked = Hands[1].Tracked = false;
            return;
        }
        // This is the input joint-gap state; gameplay success is independently observed
        // from the target's attachment and physics. No Blueprint variable names required.
        bool Pinching = false;
        for (const Hand &H : Hands)
            if (H.Tracked)
                Pinching |= (0.08 - H.Pinch * 0.078) < 0.03;
        PinchSeen |= Pinching;
        ReleaseSeen |= PinchSeen && !Pinching;
        const bool Attached = Target->GetAttachParentActor() != nullptr;
        const FVector Pos = Target->GetActorLocation(), Vel = Velocity(Target.Get());
        if (Attached && !Grabbed)
        {
            Grabbed = true;
            GrabPos = Pos;
        }
        if (Attached && Grabbed)
            HeldDistance = FMath::Max(HeldDistance, FVector::Dist(Pos, GrabPos) / 100);
        for (int Side = 0; Side < 2; ++Side)
            if (Hands[Side].Tracked && Hands[Side].Pinch > .5 && WorldVelocity[Side].Size2D() > 20)
                IntentDirection = FVector(WorldVelocity[Side].X, WorldVelocity[Side].Y, 0).GetSafeNormal();
        if (!Attached && Grabbed && !Released)
        {
            Released = true;
            ReleasePos = Pos;
            ReleaseSpeed = Vel.Size() / 100;
            ReleaseDirection = IntentDirection;
            ForwardSpeed = FVector::DotProduct(Vel, ReleaseDirection) / 100;
        }
        if (Released)
            FlightDistance = FMath::Max(FlightDistance, FVector::Dist(Pos, ReleasePos) / 100);
        if (Released)
            ForwardFlight =
                FMath::Max(ForwardFlight, FVector::DotProduct(Pos - ReleasePos, ReleaseDirection) / 100);
        Json Sample = Obj();
        Sample->SetNumberField(TEXT("t"), Elapsed);
        Sample->SetNumberField(TEXT("wall_time_s"), FPlatformTime::Seconds() - RunStart);
        Sample->SetArrayField(TEXT("pos"), V3(Pos));
        Sample->SetArrayField(TEXT("velocity"), V3(Vel));
        Sample->SetBoolField(TEXT("attached"), Attached);
        Sample->SetBoolField(TEXT("pinching"), Pinching);
        Trace.Add(MakeShared<FJsonValueObject>(Sample));
        Sample->SetArrayField(TEXT("right_input_point"), V3(Hands[1].Point));
        {
            FScopeLock Lock(&MotionMutex);
            Sample->SetArrayField(
                TEXT("right_input_velocity"),
                V3(GEngine->XRSystem->GetTrackingToWorldTransform().TransformVector(MotionAimVelocity[1])));
        }
        if (APawn *Pawn = UGameplayStatics::GetPlayerPawn(W, 0))
        {
            Sample->SetArrayField(TEXT("pawn_pos"), V3(Pawn->GetActorLocation()));
            for (UActorComponent *C : Pawn->GetComponents())
                if (UMotionControllerComponent *M = Cast<UMotionControllerComponent>(C))
                    if (MotionSide(M->MotionSource) == 1 && !M->MotionSource.ToString().EndsWith(TEXT("Aim")))
                        Sample->SetArrayField(TEXT("right_grip_world"), V3(M->GetComponentLocation()));
        }
        if (Capture && Attached && HeldDistance > .15 && !HeldShot)
        {
            FScreenshotRequest::RequestScreenshot(Dir / (RunId + TEXT("-held.png")), false, false);
            HeldShot = true;
        }
        if (Capture && Released && FlightDistance > .1 && !FlightShot)
        {
            FScreenshotRequest::RequestScreenshot(Dir / (RunId + TEXT("-flight.png")), false, false);
            FlightShot = true;
        }
        Elapsed += Dt;
        if (Elapsed >= Frames.Last().T)
        {
            Json R = Obj();
            R->SetStringField(TEXT("input_source"), TEXT("unreal_ihandtracker_injection"));
            R->SetBoolField(TEXT("vr_preview"), VR());
            R->SetStringField(TEXT("target"), Target->GetName());
            R->SetBoolField(TEXT("grabbed"), Grabbed);
            R->SetBoolField(TEXT("released"), Released);
            R->SetBoolField(TEXT("pinch_seen"), PinchSeen);
            R->SetBoolField(TEXT("release_seen"), ReleaseSeen);
            R->SetNumberField(TEXT("held_movement_m"), HeldDistance);
            R->SetNumberField(TEXT("release_speed_mps"), ReleaseSpeed);
            R->SetNumberField(TEXT("flight_distance_m"), FlightDistance);
            R->SetNumberField(TEXT("joint_reads"), Reads);
            R->SetNumberField(TEXT("max_frame_ms"), MaxDt * 1000);
            R->SetArrayField(TEXT("trace"), Trace);
            R->SetObjectField(TEXT("scenario"), RunScenario);
            R->SetStringField(TEXT("project"), FPaths::GetProjectFilePath());
            if (APawn *Pawn = UGameplayStatics::GetPlayerPawn(W, 0))
                R->SetStringField(TEXT("pawn"), Pawn->GetName());
            {
                FScopeLock Lock(&MotionMutex);
                R->SetNumberField(TEXT("grip_reads"), MotionReads);
            }
            R->SetNumberField(TEXT("release_forward_speed_mps"), ForwardSpeed);
            R->SetNumberField(TEXT("forward_flight_m"), ForwardFlight);
            R->SetArrayField(TEXT("release_direction"), V3(ReleaseDirection * 100));
            R->SetBoolField(TEXT("ok"), VR() && Reads > 0 && Grabbed && Released && PinchSeen &&
                                            ReleaseSeen && HeldDistance >= 0.1 && ForwardSpeed >= 0.2 &&
                                            ForwardFlight >= 0.05);
            Reply(RunId, R);
            RunId.Empty();
            Hands[0].Tracked = Hands[1].Tracked = false;
        }
    }

  public:
    virtual void StartupModule() override
    {
        Dir = FPlatformMisc::GetEnvironmentVariable(TEXT("XRSIM_AGENT_DIR"));
        if (Dir.IsEmpty())
            return;
        Instance = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
        IFileManager::Get().MakeDirectory(*Dir, true);
        // Most recently registered provider is first; do not unregister other providers.
        auto &Features = IModularFeatures::Get();
        Features.RegisterModularFeature(IHandTracker::GetModularFeatureName(),
                                        static_cast<IHandTracker *>(this));
        Features.RegisterModularFeature(IMotionController::GetModularFeatureName(),
                                        static_cast<IMotionController *>(this));
        TickHandle =
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FXrsimAgent::Tick));
        PreHandle = FWorldDelegates::OnWorldPreActorTick.AddRaw(this, &FXrsimAgent::Pre);
        PostHandle = FWorldDelegates::OnWorldPostActorTick.AddRaw(this, &FXrsimAgent::Post);
        UE_LOG(LogTemp, Display, TEXT("[XrsimAgent] opt-in IHandTracker test bridge: %s"), *Dir);
    }
    virtual void ShutdownModule() override
    {
        if (Dir.IsEmpty())
            return;
        Presentation.Reset();
        FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
        FWorldDelegates::OnWorldPreActorTick.Remove(PreHandle);
        FWorldDelegates::OnWorldPostActorTick.Remove(PostHandle);
        IModularFeatures::Get().UnregisterModularFeature(IHandTracker::GetModularFeatureName(),
                                                         static_cast<IHandTracker *>(this));
        IModularFeatures::Get().UnregisterModularFeature(IMotionController::GetModularFeatureName(),
                                                         static_cast<IMotionController *>(this));
    }
    int MotionSide(FName Source) const
    {
        const FString S = Source.ToString();
        if (S == TEXT("Left") || S == TEXT("LeftGrip") || S == TEXT("LeftAim"))
            return 0;
        if (S == TEXT("Right") || S == TEXT("RightGrip") || S == TEXT("RightAim"))
            return 1;
        return -1;
    }
    virtual FName GetMotionControllerDeviceTypeName() const override
    {
        return TEXT("XrsimInjectedHands");
    }
    virtual ETrackingStatus GetControllerTrackingStatus(int32 Index, FName Source) const override
    {
        FScopeLock Lock(&MotionMutex);
        const int Side = MotionSide(Source);
        return Index == 0 && Side >= 0 && MotionHands[Side].Tracked ? ETrackingStatus::Tracked
                                                                    : ETrackingStatus::NotTracked;
    }
    virtual bool GetControllerOrientationAndPosition(int32 Index, FName Source, FRotator &Rot, FVector &Pos,
                                                     float Scale) const override
    {
        FScopeLock Lock(&MotionMutex);
        const int Side = MotionSide(Source);
        if (Index != 0 || Side < 0 || !MotionHands[Side].Tracked)
            return false;
        Rot = MotionHands[Side].Quat.Rotator();
        Pos = MotionHands[Side].Point;
        if (Source.ToString().EndsWith(TEXT("Aim")))
            Pos = MotionAimPoint[Side];
        ++MotionReads;
        return true;
    }
    virtual bool GetControllerOrientationAndPosition(int32 Index, FName Source, FRotator &Rot, FVector &Pos,
                                                     bool &HasVel, FVector &Vel, bool &HasAng, FVector &Ang,
                                                     bool &HasAcc, FVector &Acc, float Scale) const override
    {
        const bool Valid = GetControllerOrientationAndPosition(Index, Source, Rot, Pos, Scale);
        HasVel = Valid;
        HasAng = HasAcc = false;
        Ang = Acc = FVector::ZeroVector;
        Vel = FVector::ZeroVector;
        if (Valid)
        {
            FScopeLock Lock(&MotionMutex);
            Vel = Source.ToString().EndsWith(TEXT("Aim")) ? MotionAimVelocity[MotionSide(Source)]
                                                          : MotionVelocity[MotionSide(Source)];
        }
        return Valid;
    }
    // UE's Blueprint GetHandTrackingState selects this exact provider name. This alias
    // is active only in the explicitly opted-in editor; evidence identifies injection.
    virtual FName GetHandTrackerDeviceTypeName() const override
    {
        return TEXT("OpenXRHandTracking");
    }
    virtual bool IsHandTrackingStateValid() const override
    {
        return Hands[0].Tracked || Hands[1].Tracked;
    }
    virtual bool GetKeypointState(EControllerHand Side, EHandKeypoint Key, FTransform &Out,
                                  float &Radius) const override
    {
        if (Side != EControllerHand::Left && Side != EControllerHand::Right)
            return false;
        const Hand &H = Hands[Side == EControllerHand::Right ? 1 : 0];
        const int K = int(Key);
        if (!H.Tracked || K < 0 || K >= 26)
            return false;
        ++Reads;
        // Anatomical joint layout in WebXR metres, anchored at the pinch midpoint. Closing the
        // thumb/index gap is the only pinch input; no gesture/EnhancedInput events are injected.
        FVector P(0, -0.025, 0.065);
        if (K == 1)
            P = FVector(0, -0.045, 0.12);
        else if (K >= 2 && K <= 5)
        {
            double A = (K - 2) / 3.0;
            P = FMath::Lerp(FVector(-0.04, -0.04, 0.075), FVector(-0.04, 0, 0), A);
            P.X += H.Pinch * 0.039 * A;
        }
        else if (K >= 6)
        {
            int Finger = (K - 6) / 5, Joint = (K - 6) % 5;
            P = FVector(0.04 + Finger * 0.025, 0.0, 0.085 - Joint * 0.02125);
            if (Finger == 0)
                P.X -= H.Pinch * 0.039 * (Joint / 4.0);
        }
        if (K >= 11)
        {
            int Finger = (K - 6) / 5, Joint = (K - 6) % 5;
            double Curl = H.Curls[Finger - 1], A = Joint / 4.0;
            P.X -= Finger * .017 * Curl;
            P.Y -= .045 * FMath::Sin(A * PI / 2) * Curl;
            P.Z += .075 * A * Curl;
        }
        if (Side == EControllerHand::Left)
            P.X = -P.X;
        FVector Offset(-P.Z * 100, P.X * 100, P.Y * 100);
        Out = FTransform(H.Quat, H.Point + H.Quat.RotateVector(Offset));
        Radius = 0.8f;
        return true;
    }
    virtual bool GetAllKeypointStates(EControllerHand Side, TArray<FVector> &Pos, TArray<FQuat> &Rot,
                                      TArray<float> &Radii, bool &Tracked) const override
    {
        Pos.Reset();
        Rot.Reset();
        Radii.Reset();
        Tracked = false;
        for (int I = 0; I < 26; ++I)
        {
            FTransform T;
            float R;
            if (!GetKeypointState(Side, EHandKeypoint(I), T, R))
                return false;
            Pos.Add(T.GetLocation());
            Rot.Add(T.GetRotation());
            Radii.Add(R);
        }
        Tracked = true;
        return true;
    }
};
IMPLEMENT_MODULE(FXrsimAgent, XrsimAgent)
