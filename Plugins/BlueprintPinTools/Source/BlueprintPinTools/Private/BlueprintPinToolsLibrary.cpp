// Copyright Alex Coulombe Presents. All Rights Reserved.

#include "BlueprintPinToolsLibrary.h"

#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "FileHelpers.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogBlueprintPinTools, Log, All);

namespace
{
	/** Every graph a Blueprint owns: event graphs, functions, macros, delegates. */
	void GatherAllGraphs(UBlueprint* Blueprint, TArray<UEdGraph*>& OutGraphs)
	{
		if (!Blueprint)
		{
			return;
		}
		OutGraphs.Append(Blueprint->UbergraphPages);
		OutGraphs.Append(Blueprint->FunctionGraphs);
		OutGraphs.Append(Blueprint->MacroGraphs);
		OutGraphs.Append(Blueprint->DelegateSignatureGraphs);
		OutGraphs.RemoveAll([](UEdGraph* G) { return G == nullptr; });
	}

	FString PathOf(const UObject* Object)
	{
		return Object ? Object->GetPathName() : FString();
	}

	/** A pin carries an object literal either as DefaultObject or as a soft path in DefaultValue. */
	bool IsObjectPin(const UEdGraphPin* Pin)
	{
		return Pin
			&& Pin->Direction == EGPD_Input
			&& Pin->LinkedTo.Num() == 0
			&& (Pin->DefaultObject != nullptr || Pin->PinType.PinCategory == TEXT("object")
				|| Pin->PinType.PinCategory == TEXT("class")
				|| Pin->PinType.PinCategory == TEXT("softobject"));
	}

	bool NodeMatches(const UEdGraphNode* Node, const FString& Identifier)
	{
		if (!Node || Identifier.IsEmpty())
		{
			return false;
		}
		if (Node->GetName().Contains(Identifier, ESearchCase::IgnoreCase))
		{
			return true;
		}
		if (Node->NodeGuid.ToString().Contains(Identifier, ESearchCase::IgnoreCase))
		{
			return true;
		}
		const FString Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
		return Title.Contains(Identifier, ESearchCase::IgnoreCase);
	}
}

FPinDefaultChangeResult UBlueprintPinToolsLibrary::SetPinDefaultObject(
	UBlueprint* Blueprint,
	const FString& NodeIdentifier,
	const FName PinName,
	UObject* NewObject,
	bool bSavePackage)
{
	FPinDefaultChangeResult Result;

	if (!Blueprint)
	{
		Result.Error = TEXT("Blueprint is null");
		return Result;
	}
	if (!NewObject)
	{
		Result.Error = TEXT("NewObject is null; refusing to clear a pin default implicitly");
		return Result;
	}
	if (NodeIdentifier.IsEmpty())
	{
		Result.Error = TEXT("NodeIdentifier is empty; refusing to match every node");
		return Result;
	}

	TArray<UEdGraph*> Graphs;
	GatherAllGraphs(Blueprint, Graphs);

	// Collect ALL matches first. Editing under ambiguity is worse than not editing.
	struct FMatch
	{
		UEdGraph* Graph = nullptr;
		UEdGraphNode* Node = nullptr;
		UEdGraphPin* Pin = nullptr;
	};
	TArray<FMatch> Matches;

	for (UEdGraph* Graph : Graphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!NodeMatches(Node, NodeIdentifier))
			{
				continue;
			}
			if (UEdGraphPin* Pin = Node->FindPin(PinName, EGPD_Input))
			{
				Matches.Add({Graph, Node, Pin});
			}
		}
	}

	if (Matches.Num() == 0)
	{
		Result.Error = FString::Printf(
			TEXT("no node matching '%s' with an input pin named '%s' in %s"),
			*NodeIdentifier, *PinName.ToString(), *Blueprint->GetName());
		return Result;
	}
	if (Matches.Num() > 1)
	{
		FString Where;
		for (const FMatch& M : Matches)
		{
			Where += FString::Printf(TEXT("\n    %s::%s"), *M.Graph->GetName(), *M.Node->GetName());
		}
		Result.Error = FString::Printf(
			TEXT("AMBIGUOUS: '%s' matched %d nodes; refusing to guess.%s"),
			*NodeIdentifier, Matches.Num(), *Where);
		return Result;
	}

	const FMatch& Match = Matches[0];
	Result.MatchedPin = FString::Printf(TEXT("%s::%s::%s"),
		*Match.Graph->GetName(), *Match.Node->GetName(), *PinName.ToString());
	Result.PreviousValue = PathOf(Match.Pin->DefaultObject);

	// Idempotent: already correct means do nothing, and say so.
	if (Match.Pin->DefaultObject == NewObject)
	{
		Result.bSucceeded = true;
		Result.bAlreadySet = true;
		Result.NewValue = Result.PreviousValue;
		UE_LOG(LogBlueprintPinTools, Log,
			TEXT("%s already set to %s -- no change"), *Result.MatchedPin, *Result.NewValue);
		return Result;
	}

	const UEdGraphSchema* Schema = Match.Pin->GetSchema();
	if (!Schema)
	{
		Result.Error = TEXT("pin has no schema");
		return Result;
	}

	Match.Node->Modify();
	Schema->TrySetDefaultObject(*Match.Pin, NewObject, /*bMarkAsModified=*/true);

	// Re-READ the pin rather than assuming the write took. TrySetDefaultObject is
	// void and silently declines a type-incompatible object.
	Result.NewValue = PathOf(Match.Pin->DefaultObject);
	if (Match.Pin->DefaultObject != NewObject)
	{
		Result.Error = FString::Printf(
			TEXT("schema declined the object (type mismatch?); pin still holds '%s'"),
			*Result.NewValue);
		return Result;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	if (bSavePackage)
	{
		if (UPackage* Package = Blueprint->GetOutermost())
		{
			Package->MarkPackageDirty();
			TArray<UPackage*> ToSave;
			ToSave.Add(Package);
			if (!FEditorFileUtils::SavePackages(ToSave, /*bOnlyDirty=*/false))
			{
				Result.Error = FString::Printf(
					TEXT("pin was set to '%s' but saving %s FAILED"),
					*Result.NewValue, *Package->GetName());
				return Result;
			}
		}
	}

	Result.bSucceeded = true;
	UE_LOG(LogBlueprintPinTools, Log, TEXT("%s: '%s' -> '%s'"),
		*Result.MatchedPin, *Result.PreviousValue, *Result.NewValue);
	return Result;
}

TArray<FString> UBlueprintPinToolsLibrary::FindObjectPinDefaults(
	UBlueprint* Blueprint,
	const FString& ObjectPathFilter)
{
	TArray<FString> Out;
	if (!Blueprint)
	{
		return Out;
	}

	TArray<UEdGraph*> Graphs;
	GatherAllGraphs(Blueprint, Graphs);

	for (UEdGraph* Graph : Graphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!IsObjectPin(Pin) || !Pin->DefaultObject)
				{
					continue;
				}
				const FString Value = PathOf(Pin->DefaultObject);
				if (!ObjectPathFilter.IsEmpty()
					&& !Value.Contains(ObjectPathFilter, ESearchCase::IgnoreCase))
				{
					continue;
				}
				Out.Add(FString::Printf(TEXT("%s::%s::%s = %s"),
					*Graph->GetName(), *Node->GetName(), *Pin->PinName.ToString(), *Value));
			}
		}
	}
	return Out;
}
