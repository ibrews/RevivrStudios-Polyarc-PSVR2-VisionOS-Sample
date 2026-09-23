// Copyright Alex Coulombe Presents. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BlueprintPinToolsLibrary.generated.h"

class UBlueprint;

/**
 * Result of a pin-default write, so callers can verify rather than trust.
 * Every field is populated even on failure, so a Python caller can print a
 * meaningful before/after without a second round trip.
 */
USTRUCT(BlueprintType)
struct FPinDefaultChangeResult
{
	GENERATED_BODY()

	/** True only when the pin was found AND now holds the requested object. */
	UPROPERTY(BlueprintReadOnly, Category = "Blueprint Pin Tools")
	bool bSucceeded = false;

	/** True when the pin already held the requested object and nothing was written. */
	UPROPERTY(BlueprintReadOnly, Category = "Blueprint Pin Tools")
	bool bAlreadySet = false;

	/** Object path the pin held before the call ("" if none). */
	UPROPERTY(BlueprintReadOnly, Category = "Blueprint Pin Tools")
	FString PreviousValue;

	/** Object path the pin holds after the call, re-read from the pin ("" if none). */
	UPROPERTY(BlueprintReadOnly, Category = "Blueprint Pin Tools")
	FString NewValue;

	/** Graph::Node::Pin actually matched, for logging. */
	UPROPERTY(BlueprintReadOnly, Category = "Blueprint Pin Tools")
	FString MatchedPin;

	/** Human-readable reason when bSucceeded is false. */
	UPROPERTY(BlueprintReadOnly, Category = "Blueprint Pin Tools")
	FString Error;
};

/**
 * Editor-only Blueprint graph PIN DEFAULT access for scripting.
 *
 * WHY THIS EXISTS: stock UE Python can list a Blueprint's graphs and nodes
 * (UEdGraph::Nodes is a UPROPERTY), but it cannot read or write a pin's default
 * value, because UEdGraphPin is a plain struct rather than a UObject and is
 * therefore absent from the reflection surface Python sees. Any headless task
 * that needs to retarget a literal on a node -- swapping a sound on a
 * PlaySound2D node, repointing a class or data asset -- hits that wall.
 */
UCLASS()
class UBlueprintPinToolsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Set an object-typed pin's default on a node inside a Blueprint, then mark
	 * the Blueprint modified, compile it, and optionally save the package.
	 *
	 * Matching is deliberately forgiving because node names in a cooked-from-editor
	 * graph are generated: NodeIdentifier matches a node whose name, GUID string, or
	 * title CONTAINS the given text (case-insensitive). If more than one node matches,
	 * the call FAILS rather than guessing -- an ambiguous edit in a build pipeline is
	 * worse than no edit.
	 *
	 * @param Blueprint       The Blueprint asset to modify.
	 * @param NodeIdentifier  Substring of the node's name, GUID, or title.
	 * @param PinName         Exact pin name (e.g. "Sound").
	 * @param NewObject       Object to assign. Must satisfy the pin's type.
	 * @param bSavePackage    Save the package when a change was actually written.
	 * @return                Before/after values and a reason on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Pin Tools",
		meta = (DisplayName = "Set Pin Default Object"))
	static FPinDefaultChangeResult SetPinDefaultObject(
		UBlueprint* Blueprint,
		const FString& NodeIdentifier,
		const FName PinName,
		UObject* NewObject,
		bool bSavePackage = true);

	/**
	 * Read-only survey: every object-typed pin default in the Blueprint whose current
	 * value's path contains ObjectPathFilter (pass "" for all). Use this to find what
	 * to target before writing anything.
	 *
	 * @return Lines of the form "Graph::Node::Pin = /Object/Path".
	 */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Pin Tools",
		meta = (DisplayName = "Find Object Pin Defaults"))
	static TArray<FString> FindObjectPinDefaults(
		UBlueprint* Blueprint,
		const FString& ObjectPathFilter);
};
