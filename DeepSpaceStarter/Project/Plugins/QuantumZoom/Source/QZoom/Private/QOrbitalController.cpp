#include "QOrbitalController.h"
#include "NiagaraComponent.h"
#include "IDisplayCluster.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"

const FString AQOrbitalController::ClusterEventName = TEXT("QOrbital.Param");

AQOrbitalController::AQOrbitalController()
{
	PrimaryActorTick.bCanEverTick = false;
}

UNiagaraComponent* AQOrbitalController::ResolveNiagaraComponent()
{
	if (IsValid(NiagaraComp)) return NiagaraComp;

	if (AActor* Target = TargetActor.LoadSynchronous())
	{
		NiagaraComp = Target->FindComponentByClass<UNiagaraComponent>();
		if (NiagaraComp)
		{
			UE_LOG(LogTemp, Log, TEXT("[QOrbitalController] Bound to '%s'"), *GetNameSafe(Target));
			return NiagaraComp;
		}
		UE_LOG(LogTemp, Warning, TEXT("[QOrbitalController] TargetActor '%s' has no UNiagaraComponent"),
			*GetNameSafe(Target));
	}

	// Fallback: first NiagaraComponent in world
	UWorld* W = GetWorld();
	if (!W) return nullptr;
	for (TObjectIterator<UNiagaraComponent> It; It; ++It)
	{
		if (UNiagaraComponent* NC = *It)
		{
			if (NC->GetWorld() == W && IsValid(NC->GetOwner()))
			{
				NiagaraComp = NC;
				UE_LOG(LogTemp, Log, TEXT("[QOrbitalController] Auto-discovered NiagaraComponent on '%s'"),
					*GetNameSafe(NC->GetOwner()));
				return NiagaraComp;
			}
		}
	}
	return nullptr;
}

void AQOrbitalController::BeginPlay()
{
	Super::BeginPlay();

	if (IDisplayCluster::IsAvailable())
	{
		bIsPrimary = IDisplayCluster::Get().GetClusterMgr()->IsPrimary();
		ClusterEventDelegate = FOnClusterEventJsonListener::CreateUObject(this, &AQOrbitalController::OnClusterEventJson);
		IDisplayCluster::Get().GetClusterMgr()->AddClusterEventJsonListener(ClusterEventDelegate);
		UE_LOG(LogTemp, Log, TEXT("[QOrbitalController] Cluster role: %s"), bIsPrimary ? TEXT("PRIMARY") : TEXT("SECONDARY"));
	}
	else
	{
		bIsPrimary = true;
		UE_LOG(LogTemp, Log, TEXT("[QOrbitalController] No cluster — assumed PRIMARY"));
	}

	if (!ResolveNiagaraComponent())
	{
		UE_LOG(LogTemp, Warning, TEXT("[QOrbitalController] No NiagaraComponent on TargetActor — set in Details panel"));
		return;
	}

	// Defer apply by 0.2s so Niagara System has time to fully activate before SetVariable calls.
	// Both primary and secondary apply level-serialized properties directly (no cluster-event reliance).
	FTimerHandle DummyHandle;
	GetWorld()->GetTimerManager().SetTimer(DummyHandle,
		FTimerDelegate::CreateUObject(this, &AQOrbitalController::ApplyAllDirect),
		0.2f, false);
}

void AQOrbitalController::ApplyAllDirect()
{
	UE_LOG(LogTemp, Log, TEXT("[QOrbitalController] ApplyAllDirect (deferred): node=%s"), bIsPrimary ? TEXT("PRIMARY") : TEXT("SECONDARY"));
	ApplyFloat(TEXT("Hybridization"),  Hybridization);
	ApplyFloat(TEXT("CloudFuzziness"), CloudFuzziness);
	ApplyFloat(TEXT("MaxRadius"),      MaxRadius);
	ApplyFloat(TEXT("LayerGradient"),  LayerGradient);
	ApplyFloat(TEXT("SwitchProgress"), SwitchProgress);
	ApplyFloat(TEXT("SpawnDensity"),   SpawnDensity);
	ApplyColor(TEXT("CoreColor"),      CoreColor);
	ApplyColor(TEXT("EdgeColor"),      EdgeColor);
	ApplyFloat(TEXT("ColorCoreEdge"),  ColorCoreEdge);
	ApplyFloat(TEXT("ColorEdgeEdge"),  ColorEdgeEdge);
	ApplyActive(bActive);
}

void AQOrbitalController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IDisplayCluster::IsAvailable())
		IDisplayCluster::Get().GetClusterMgr()->RemoveClusterEventJsonListener(ClusterEventDelegate);
	Super::EndPlay(EndPlayReason);
}

void AQOrbitalController::ApplyAll()
{
	BroadcastFloat(TEXT("Hybridization"),  Hybridization);
	BroadcastFloat(TEXT("CloudFuzziness"), CloudFuzziness);
	BroadcastFloat(TEXT("MaxRadius"),      MaxRadius);
	BroadcastFloat(TEXT("LayerGradient"),  LayerGradient);
	BroadcastFloat(TEXT("SwitchProgress"), SwitchProgress);
	BroadcastFloat(TEXT("SpawnDensity"),   SpawnDensity);
	BroadcastColor(TEXT("CoreColor"),      CoreColor);
	BroadcastColor(TEXT("EdgeColor"),      EdgeColor);
	BroadcastFloat(TEXT("ColorCoreEdge"),  ColorCoreEdge);
	BroadcastFloat(TEXT("ColorEdgeEdge"),  ColorEdgeEdge);
	BroadcastBool (TEXT("Active"),         bActive);
}

#if WITH_EDITOR
void AQOrbitalController::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!HasActorBegunPlay() || !PropertyChangedEvent.Property) return;
	const FName N = PropertyChangedEvent.Property->GetFName();

	if      (N == GET_MEMBER_NAME_CHECKED(AQOrbitalController, Hybridization))  BroadcastFloat(TEXT("Hybridization"),  Hybridization);
	else if (N == GET_MEMBER_NAME_CHECKED(AQOrbitalController, CloudFuzziness)) BroadcastFloat(TEXT("CloudFuzziness"), CloudFuzziness);
	else if (N == GET_MEMBER_NAME_CHECKED(AQOrbitalController, MaxRadius))      BroadcastFloat(TEXT("MaxRadius"),      MaxRadius);
	else if (N == GET_MEMBER_NAME_CHECKED(AQOrbitalController, LayerGradient))  BroadcastFloat(TEXT("LayerGradient"),  LayerGradient);
	else if (N == GET_MEMBER_NAME_CHECKED(AQOrbitalController, SwitchProgress)) BroadcastFloat(TEXT("SwitchProgress"), SwitchProgress);
	else if (N == GET_MEMBER_NAME_CHECKED(AQOrbitalController, SpawnDensity))   BroadcastFloat(TEXT("SpawnDensity"),   SpawnDensity);
	else if (N == GET_MEMBER_NAME_CHECKED(AQOrbitalController, CoreColor))      BroadcastColor(TEXT("CoreColor"),      CoreColor);
	else if (N == GET_MEMBER_NAME_CHECKED(AQOrbitalController, EdgeColor))      BroadcastColor(TEXT("EdgeColor"),      EdgeColor);
	else if (N == GET_MEMBER_NAME_CHECKED(AQOrbitalController, ColorCoreEdge))  BroadcastFloat(TEXT("ColorCoreEdge"),  ColorCoreEdge);
	else if (N == GET_MEMBER_NAME_CHECKED(AQOrbitalController, ColorEdgeEdge))  BroadcastFloat(TEXT("ColorEdgeEdge"),  ColorEdgeEdge);
	else if (N == GET_MEMBER_NAME_CHECKED(AQOrbitalController, bActive))        BroadcastBool (TEXT("Active"),         bActive);
}
#endif

// ─── Broadcast helpers ────────────────────────────────────────────────────────

void AQOrbitalController::BroadcastFloat(const FString& ParamName, float Value)
{
	if (!IDisplayCluster::IsAvailable())
	{
		ApplyFloat(ParamName, Value);
		return;
	}
	FDisplayClusterClusterEventJson Event;
	Event.Name     = ClusterEventName;
	Event.Type     = TEXT("Param");
	Event.Category = TEXT("QOrbital");
	Event.bShouldDiscardOnRepeat = false;
	Event.Parameters.Add(TEXT("param"), ParamName);
	Event.Parameters.Add(TEXT("type"),  TEXT("float"));
	Event.Parameters.Add(TEXT("v"),     FString::SanitizeFloat(Value));
	IDisplayCluster::Get().GetClusterMgr()->EmitClusterEventJson(Event, false);
}

void AQOrbitalController::BroadcastColor(const FString& ParamName, const FLinearColor& Value)
{
	if (!IDisplayCluster::IsAvailable())
	{
		ApplyColor(ParamName, Value);
		return;
	}
	FDisplayClusterClusterEventJson Event;
	Event.Name     = ClusterEventName;
	Event.Type     = TEXT("Param");
	Event.Category = TEXT("QOrbital");
	Event.bShouldDiscardOnRepeat = false;
	Event.Parameters.Add(TEXT("param"), ParamName);
	Event.Parameters.Add(TEXT("type"),  TEXT("color"));
	Event.Parameters.Add(TEXT("r"), FString::SanitizeFloat(Value.R));
	Event.Parameters.Add(TEXT("g"), FString::SanitizeFloat(Value.G));
	Event.Parameters.Add(TEXT("b"), FString::SanitizeFloat(Value.B));
	Event.Parameters.Add(TEXT("a"), FString::SanitizeFloat(Value.A));
	IDisplayCluster::Get().GetClusterMgr()->EmitClusterEventJson(Event, false);
}

void AQOrbitalController::BroadcastBool(const FString& ParamName, bool Value)
{
	if (!IDisplayCluster::IsAvailable())
	{
		if (ParamName == TEXT("Active")) ApplyActive(Value);
		return;
	}
	FDisplayClusterClusterEventJson Event;
	Event.Name     = ClusterEventName;
	Event.Type     = TEXT("Param");
	Event.Category = TEXT("QOrbital");
	Event.bShouldDiscardOnRepeat = false;
	Event.Parameters.Add(TEXT("param"), ParamName);
	Event.Parameters.Add(TEXT("type"),  TEXT("bool"));
	Event.Parameters.Add(TEXT("v"),     Value ? TEXT("1") : TEXT("0"));
	IDisplayCluster::Get().GetClusterMgr()->EmitClusterEventJson(Event, false);
}

// ─── Listener + apply ─────────────────────────────────────────────────────────

void AQOrbitalController::OnClusterEventJson(const FDisplayClusterClusterEventJson& Event)
{
	if (Event.Name != ClusterEventName) return;

	const FString ParamName = Event.Parameters.FindRef(TEXT("param"));
	const FString TypeStr   = Event.Parameters.FindRef(TEXT("type"));

	if (TypeStr == TEXT("float"))
	{
		ApplyFloat(ParamName, FCString::Atof(*Event.Parameters.FindRef(TEXT("v"))));
	}
	else if (TypeStr == TEXT("color"))
	{
		FLinearColor C;
		C.R = FCString::Atof(*Event.Parameters.FindRef(TEXT("r")));
		C.G = FCString::Atof(*Event.Parameters.FindRef(TEXT("g")));
		C.B = FCString::Atof(*Event.Parameters.FindRef(TEXT("b")));
		C.A = FCString::Atof(*Event.Parameters.FindRef(TEXT("a")));
		ApplyColor(ParamName, C);
	}
	else if (TypeStr == TEXT("bool") && ParamName == TEXT("Active"))
	{
		ApplyActive(Event.Parameters.FindRef(TEXT("v")) == TEXT("1"));
	}
}

void AQOrbitalController::ApplyFloat(const FString& ParamName, float Value)
{
	UNiagaraComponent* NC = ResolveNiagaraComponent();
	if (!NC)
	{
		UE_LOG(LogTemp, Warning, TEXT("[QOrbitalController] ApplyFloat User.%s = %f — NO NIAGARA COMP"), *ParamName, Value);
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("[QOrbitalController] ApplyFloat User.%s = %f on '%s'"), *ParamName, Value, *GetNameSafe(NC->GetOwner()));
	NC->SetVariableFloat(FName(*FString::Printf(TEXT("User.%s"), *ParamName)), Value);
}

void AQOrbitalController::ApplyColor(const FString& ParamName, const FLinearColor& Value)
{
	UNiagaraComponent* NC = ResolveNiagaraComponent();
	if (!NC)
	{
		UE_LOG(LogTemp, Warning, TEXT("[QOrbitalController] ApplyColor User.%s — NO NIAGARA COMP"), *ParamName);
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("[QOrbitalController] ApplyColor User.%s = (%.2f,%.2f,%.2f,%.2f)"), *ParamName, Value.R, Value.G, Value.B, Value.A);
	NC->SetVariableLinearColor(FName(*FString::Printf(TEXT("User.%s"), *ParamName)), Value);
}

void AQOrbitalController::ApplyActive(bool bNewActive)
{
	UNiagaraComponent* NC = ResolveNiagaraComponent();
	if (!NC) return;
	UE_LOG(LogTemp, Log, TEXT("[QOrbitalController] ApplyActive = %s"), bNewActive ? TEXT("true") : TEXT("false"));
	if (bNewActive) NC->Activate();
	else            NC->Deactivate();
}
