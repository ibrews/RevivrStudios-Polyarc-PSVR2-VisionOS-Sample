#pragma once
#include "CoreMinimal.h"
class AActor;
class IHandTracker;
class UStaticMeshComponent;

// Direct joint geometry: no skinning, rig axes, collisions or gameplay setters.
class FXrsimHandMeshes
{
    TWeakObjectPtr<UStaticMeshComponent> Joints[2][26];
    TWeakObjectPtr<UStaticMeshComponent> Bones[2][26];
  public:
    bool Create(AActor *Owner, FString &Error);
    void Update(IHandTracker *Tracker);
    void Reset();
};
