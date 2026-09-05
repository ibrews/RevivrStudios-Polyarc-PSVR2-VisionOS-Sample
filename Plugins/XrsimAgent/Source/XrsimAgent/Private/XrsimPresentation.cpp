#include "XrsimPresentation.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PointLightComponent.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "IHeadMountedDisplay.h"
#include "ISpectatorScreenController.h"
#include "IXRTrackingSystem.h"

namespace
{
ISpectatorScreenController *Spectator()
{
    if (GEngine && GEngine->XRSystem.IsValid())
        if (auto *HMD = GEngine->XRSystem->GetHMDDevice())
            return HMD->GetSpectatorScreenController();
    return nullptr;
}
bool Position(const TSharedPtr<FJsonObject> &J, const TCHAR *Key, FVector &Out)
{
    const TArray<TSharedPtr<FJsonValue>> *A;
    if (!J->TryGetArrayField(Key, A) || A->Num() != 3)
        return false;
    double V[3];
    for (int I = 0; I < 3; ++I)
        if (!(*A)[I]->TryGetNumber(V[I]) || !FMath::IsFinite(V[I]))
            return false;
    Out = FVector(-V[2], V[0], V[1]) * 100;
    return true;
}
}

bool FXrsimPresentation::Configure(UWorld *World, const TSharedPtr<FJsonObject> &Config, FString &Error)
{
    bool Enabled = true;
    Config->TryGetBoolField(TEXT("enabled"), Enabled);
    if (!Enabled)
    {
        Reset();
        return true;
    }
    FString Meshes;
    FVector PositionWorld, LookAt;
    const TSharedPtr<FJsonObject> *C;
    double Fov = 55, Width = 1280, Height = 720;
    if (!World || !Config->TryGetStringField(TEXT("meshes"), Meshes) ||
        (Meshes != TEXT("bones") && Meshes != TEXT("none")))
    {
        Error = TEXT("view requires a play world and meshes: bones or none");
        return false;
    }
    bool HasCamera = Config->TryGetObjectField(TEXT("camera"), C);
    if (HasCamera)
    {
        (*C)->TryGetNumberField(TEXT("fov"), Fov);
        (*C)->TryGetNumberField(TEXT("width"), Width);
        (*C)->TryGetNumberField(TEXT("height"), Height);
        if (!Position(*C, TEXT("pos"), PositionWorld) || !Position(*C, TEXT("lookAt"), LookAt) ||
            PositionWorld.Equals(LookAt) || !FMath::IsFinite(Fov) || Fov < 20 || Fov > 120 ||
            !FMath::IsFinite(Width) || Width < 320 || Width > 1920 ||
            !FMath::IsFinite(Height) || Height < 180 || Height > 1080 || !Spectator())
        {
            Error = TEXT("invalid camera or spectator screen unavailable");
            return false;
        }
    }
    Reset();
    FActorSpawnParameters Params;
    Params.ObjectFlags = RF_Transient;
    Params.Name = MakeUniqueObjectName(World, AActor::StaticClass(), TEXT("XrsimPresentation"));
    AActor *A = World->SpawnActor<AActor>(Params);
    Actor = A;
    A->SetActorEnableCollision(false);
    USceneComponent *Root = NewObject<USceneComponent>(A, NAME_None, RF_Transient);
    A->SetRootComponent(Root);
    Root->RegisterComponent();
    if (Meshes == TEXT("bones") && !HandMeshes.Create(A, Error))
    { Reset(); return false; }
    if (HasCamera)
    {
        auto *S = Spectator();
        PreviousMode = S->GetSpectatorScreenMode();
        PreviousTexture = S->GetSpectatorScreenTexture();
        ChangedSpectator = true;
        auto *Capture = NewObject<USceneCaptureComponent2D>(A, NAME_None, RF_Transient);
        Capture->SetupAttachment(Root);
        Capture->SetWorldLocationAndRotation(PositionWorld, (LookAt - PositionWorld).Rotation());
        Capture->FOVAngle = Fov;
        Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
        Capture->bCaptureEveryFrame = false;
        Capture->bCaptureOnMovement = false;
        Capture->bAlwaysPersistRenderingState = true;
        // Keep the small joint markers readable in brightly lit test scenes.
        Capture->PostProcessSettings.bOverride_AutoExposureBias = true;
        Capture->PostProcessSettings.AutoExposureBias = 0;
        Capture->PostProcessSettings.bOverride_BloomIntensity = true;
        Capture->PostProcessSettings.BloomIntensity = 0;
        auto *Texture = NewObject<UTextureRenderTarget2D>(Capture, NAME_None, RF_Transient);
        Texture->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
        Texture->InitAutoFormat(int32(Width), int32(Height));
        Capture->TextureTarget = Texture;
        A->AddInstanceComponent(Capture);
        Capture->RegisterComponent();
        bool Fill = true;
        (*C)->TryGetBoolField(TEXT("fillLight"), Fill);
        if (Fill)
        {
            auto *Light = NewObject<UPointLightComponent>(A, NAME_None, RF_Transient);
            Light->SetupAttachment(Root);
            Light->SetWorldLocation(PositionWorld);
            Light->SetIntensityUnits(ELightUnits::Lumens);
            Light->SetIntensity(500);
            Light->SetAttenuationRadius(500);
            Light->SetCastShadows(false);
            A->AddInstanceComponent(Light);
            Light->RegisterComponent();
        }
        Camera = Capture;
        Tick(World);
    }
    Settings = Config;
    return true;
}

void FXrsimPresentation::Reset()
{
    HandMeshes.Reset();
    if (ChangedSpectator)
        if (auto *S = Spectator())
        {
            S->SetSpectatorScreenTexture(PreviousTexture.Get());
            S->SetSpectatorScreenMode(PreviousMode);
        }
    ChangedSpectator = false;
    if (Actor.IsValid())
        Actor->Destroy();
    Actor.Reset();
    Camera.Reset();
    Settings.Reset();
}

void FXrsimPresentation::Tick(UWorld *World)
{
    if (!World || !Actor.IsValid() || Actor->GetWorld() != World)
    {
        if (ChangedSpectator || Actor.IsValid())
            Reset();
        return;
    }
    if (Camera.IsValid())
        if (auto *S = Spectator())
        {
            S->SetSpectatorScreenTexture(Camera->TextureTarget);
            S->SetSpectatorScreenMode(ESpectatorScreenMode::Texture);
        }
}

TSharedPtr<FJsonObject> FXrsimPresentation::State() const
{
    auto J = MakeShared<FJsonObject>();
    J->SetBoolField(TEXT("enabled"), Actor.IsValid());
    J->SetBoolField(TEXT("spectator"), Camera.IsValid());
    if (Settings.IsValid())
        J->SetObjectField(TEXT("settings"), Settings);
    return J;
}

void FXrsimPresentation::UpdateHands(IHandTracker *Tracker)
{
    HandMeshes.Update(Tracker);
    // Capture after gameplay and the joint geometry have updated. A transient
    // actor's capture component must not depend on actor-tick registration.
    if (Camera.IsValid())
        Camera->CaptureScene();
}
