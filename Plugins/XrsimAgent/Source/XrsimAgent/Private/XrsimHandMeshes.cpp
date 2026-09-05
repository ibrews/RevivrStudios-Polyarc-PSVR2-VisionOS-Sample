#include "XrsimHandMeshes.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "IHandTracker.h"
#include "Materials/Material.h"

namespace
{
// Unreal/OpenXR order: palm, wrist, thumb chain, four finger chains.
const int Parent[26] = {1,-1,1,2,3,4,1,6,7,8,9,1,11,12,13,14,1,16,17,18,19,1,21,22,23,24};
UMaterial *ColorMaterial(AActor *Owner, FColor Color)
{
    auto *M = NewObject<UMaterial>(Owner, NAME_None, RF_Transient);
    M->SetShadingModel(MSM_Unlit);
    M->GetEditorOnlyData()->EmissiveColor.UseConstant = true;
    M->GetEditorOnlyData()->EmissiveColor.Constant = Color;
    M->PostEditChange();
    return M;
}
UStaticMeshComponent *Part(AActor *Owner, UStaticMesh *Mesh, UMaterial *Material)
{
    auto *C = NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transient);
    C->SetupAttachment(Owner->GetRootComponent());
    C->SetMobility(EComponentMobility::Movable);
    C->SetStaticMesh(Mesh);
    C->SetMaterial(0, Material);
    C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    C->SetGenerateOverlapEvents(false);
    C->SetCastShadow(false);
    C->SetVisibility(false);
    Owner->AddInstanceComponent(C);
    C->RegisterComponent();
    return C;
}
}
bool FXrsimHandMeshes::Create(AActor *Owner, FString &Error)
{
    Reset();
    auto *Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    auto *Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    if (!Sphere || !Cylinder) { Error = TEXT("Engine basic shapes are missing"); return false; }
    auto *Tips = ColorMaterial(Owner, FColor(240, 250, 255));
    for (int S = 0; S < 2; ++S)
    {
        auto *Material = ColorMaterial(Owner, S ? FColor(24, 195, 240) : FColor(255, 158, 36));
        for (int K = 0; K < 26; ++K)
        {
            Joints[S][K] = Part(Owner, Sphere, (K == 5 || K == 10) ? Tips : Material);
            if (Parent[K] >= 0) Bones[S][K] = Part(Owner, Cylinder, Material);
        }
    }
    return true;
}
void FXrsimHandMeshes::Update(IHandTracker *Tracker)
{
    for (int S = 0; S < 2; ++S)
    {
        if (!Joints[S][0].IsValid()) continue;
        TArray<FVector> Pos; TArray<FQuat> Rot; TArray<float> Radius; bool Tracked = false;
        const bool Valid = Tracker && Tracker->GetAllKeypointStates(S ? EControllerHand::Right : EControllerHand::Left, Pos, Rot, Radius, Tracked) && Tracked && Pos.Num() == 26;
        for (int K = 0; K < 26; ++K)
        {
            auto *Joint = Joints[S][K].Get(); auto *Bone = Bones[S][K].Get();
            if (Joint) Joint->SetVisibility(Valid);
            if (Bone) Bone->SetVisibility(Valid);
            if (!Valid) continue;
            // Engine shapes have 100 cm diameter/height. Finger joints are 9 mm
            // wide; cylinders terminate at the exact consumed joint positions.
            const float Scale = K < 2 ? .018f : .009f;
            if (Joint) Joint->SetWorldTransform(FTransform(FQuat::Identity, Pos[K], FVector(Scale)));
            if (Bone)
            {
                const FVector Delta = Pos[K] - Pos[Parent[K]];
                if (Delta.SizeSquared() < .0001) { Bone->SetVisibility(false); continue; }
                Bone->SetWorldTransform(FTransform(FQuat::FindBetweenNormals(FVector::UpVector, Delta.GetSafeNormal()),
                    (Pos[K] + Pos[Parent[K]]) * .5, FVector(.006, .006, Delta.Size() / 100)));
            }
        }
    }
}
void FXrsimHandMeshes::Reset()
{
    for (int S = 0; S < 2; ++S)
        for (int K = 0; K < 26; ++K)
        {
            if (Joints[S][K].IsValid()) Joints[S][K]->DestroyComponent();
            if (Bones[S][K].IsValid()) Bones[S][K]->DestroyComponent();
            Joints[S][K].Reset(); Bones[S][K].Reset();
        }
}
