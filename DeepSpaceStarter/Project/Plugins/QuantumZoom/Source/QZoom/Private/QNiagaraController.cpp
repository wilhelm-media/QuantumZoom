#include "QNiagaraController.h"
#include "NiagaraComponent.h"
#include "IDisplayCluster.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"

const FString AQNiagaraController::ClusterEventName = TEXT("QNiagara.Param");

AQNiagaraController::AQNiagaraController()
{
	PrimaryActorTick.bCanEverTick = false;
}

UNiagaraComponent* AQNiagaraController::ResolveNiagaraComponent()
{
	if (IsValid(NiagaraComp)) return NiagaraComp;

	// Try the explicit TargetActor first
	if (AActor* Target = TargetActor.LoadSynchronous())
	{
		NiagaraComp = Target->FindComponentByClass<UNiagaraComponent>();
		if (NiagaraComp)
		{
			UE_LOG(LogTemp, Log, TEXT("[QNiagaraController] Found NiagaraComponent on TargetActor '%s'"),
				*GetNameSafe(Target));
			return NiagaraComp;
		}
		UE_LOG(LogTemp, Warning, TEXT("[QNiagaraController] TargetActor '%s' has no UNiagaraComponent"),
			*GetNameSafe(Target));
	}

	// Fallback: find first NiagaraComponent in the world
	UWorld* W = GetWorld();
	if (!W) return nullptr;
	for (TObjectIterator<UNiagaraComponent> It; It; ++It)
	{
		if (UNiagaraComponent* NC = *It)
		{
			if (NC->GetWorld() == W && IsValid(NC->GetOwner()))
			{
				NiagaraComp = NC;
				UE_LOG(LogTemp, Log, TEXT("[QNiagaraController] Auto-discovered NiagaraComponent on '%s' (TargetActor was unset/invalid)"),
					*GetNameSafe(NC->GetOwner()));
				return NiagaraComp;
			}
		}
	}
	return nullptr;
}

void AQNiagaraController::BeginPlay()
{
	Super::BeginPlay();

	if (IDisplayCluster::IsAvailable())
	{
		bIsPrimary = IDisplayCluster::Get().GetClusterMgr()->IsPrimary();
		ClusterEventDelegate = FOnClusterEventJsonListener::CreateUObject(this, &AQNiagaraController::OnClusterEventJson);
		IDisplayCluster::Get().GetClusterMgr()->AddClusterEventJsonListener(ClusterEventDelegate);
	}
	else
	{
		bIsPrimary = true;
	}

	if (!ResolveNiagaraComponent())
	{
		UE_LOG(LogTemp, Warning, TEXT("[QNiagaraController] No NiagaraComponent on TargetActor — set in Details panel"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[QNiagaraController] Bound to %s | Primary: %s"),
		*GetNameSafe(TargetActor.LoadSynchronous()), bIsPrimary ? TEXT("YES") : TEXT("NO"));

	// Primary broadcasts canonical state to all nodes; secondaries apply level-saved
	// defaults locally until the broadcast arrives (typically same frame)
	if (bIsPrimary)
	{
		ApplyAll();
	}
	else
	{
		ApplyFloat (TEXT("VectorFieldIntensity"), VectorFieldIntensity);
		ApplyFloat (TEXT("SpawnRate"),            SpawnRate);
		ApplyFloat (TEXT("Lifetime"),             Lifetime);
		ApplyFloat (TEXT("SpriteSize"),           SpriteSize);
		ApplyFloat (TEXT("EmitterRadius"),        EmitterRadius);
		ApplyColor (TEXT("ParticleColor"),        ParticleColor);
		ApplyActive(bActive);
	}
}

void AQNiagaraController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IDisplayCluster::IsAvailable())
		IDisplayCluster::Get().GetClusterMgr()->RemoveClusterEventJsonListener(ClusterEventDelegate);
	Super::EndPlay(EndPlayReason);
}

void AQNiagaraController::ApplyAll()
{
	BroadcastFloat(TEXT("VectorFieldIntensity"), VectorFieldIntensity);
	BroadcastFloat(TEXT("SpawnRate"),            SpawnRate);
	BroadcastFloat(TEXT("Lifetime"),             Lifetime);
	BroadcastFloat(TEXT("SpriteSize"),           SpriteSize);
	BroadcastFloat(TEXT("EmitterRadius"),        EmitterRadius);
	BroadcastColor(TEXT("ParticleColor"),        ParticleColor);
	BroadcastBool (TEXT("Active"),               bActive);
}

#if WITH_EDITOR
void AQNiagaraController::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!HasActorBegunPlay() || !PropertyChangedEvent.Property) return;

	const FName N = PropertyChangedEvent.Property->GetFName();

	if      (N == GET_MEMBER_NAME_CHECKED(AQNiagaraController, VectorFieldIntensity)) BroadcastFloat(TEXT("VectorFieldIntensity"), VectorFieldIntensity);
	else if (N == GET_MEMBER_NAME_CHECKED(AQNiagaraController, SpawnRate))            BroadcastFloat(TEXT("SpawnRate"),            SpawnRate);
	else if (N == GET_MEMBER_NAME_CHECKED(AQNiagaraController, Lifetime))             BroadcastFloat(TEXT("Lifetime"),             Lifetime);
	else if (N == GET_MEMBER_NAME_CHECKED(AQNiagaraController, SpriteSize))           BroadcastFloat(TEXT("SpriteSize"),           SpriteSize);
	else if (N == GET_MEMBER_NAME_CHECKED(AQNiagaraController, EmitterRadius))        BroadcastFloat(TEXT("EmitterRadius"),        EmitterRadius);
	else if (N == GET_MEMBER_NAME_CHECKED(AQNiagaraController, ParticleColor))        BroadcastColor(TEXT("ParticleColor"),        ParticleColor);
	else if (N == GET_MEMBER_NAME_CHECKED(AQNiagaraController, bActive))              BroadcastBool (TEXT("Active"),               bActive);
}
#endif

// ─── Broadcast helpers ────────────────────────────────────────────────────────

void AQNiagaraController::BroadcastFloat(const FString& ParamName, float Value)
{
	if (!IDisplayCluster::IsAvailable())
	{
		ApplyFloat(ParamName, Value);
		return;
	}
	FDisplayClusterClusterEventJson Event;
	Event.Name     = ClusterEventName;
	Event.Type     = TEXT("Param");
	Event.Category = TEXT("QNiagara");
	Event.bShouldDiscardOnRepeat = false;
	Event.Parameters.Add(TEXT("param"), ParamName);
	Event.Parameters.Add(TEXT("type"),  TEXT("float"));
	Event.Parameters.Add(TEXT("v"),     FString::SanitizeFloat(Value));
	IDisplayCluster::Get().GetClusterMgr()->EmitClusterEventJson(Event, false);
}

void AQNiagaraController::BroadcastColor(const FString& ParamName, const FLinearColor& Value)
{
	if (!IDisplayCluster::IsAvailable())
	{
		ApplyColor(ParamName, Value);
		return;
	}
	FDisplayClusterClusterEventJson Event;
	Event.Name     = ClusterEventName;
	Event.Type     = TEXT("Param");
	Event.Category = TEXT("QNiagara");
	Event.bShouldDiscardOnRepeat = false;
	Event.Parameters.Add(TEXT("param"), ParamName);
	Event.Parameters.Add(TEXT("type"),  TEXT("color"));
	Event.Parameters.Add(TEXT("r"), FString::SanitizeFloat(Value.R));
	Event.Parameters.Add(TEXT("g"), FString::SanitizeFloat(Value.G));
	Event.Parameters.Add(TEXT("b"), FString::SanitizeFloat(Value.B));
	Event.Parameters.Add(TEXT("a"), FString::SanitizeFloat(Value.A));
	IDisplayCluster::Get().GetClusterMgr()->EmitClusterEventJson(Event, false);
}

void AQNiagaraController::BroadcastBool(const FString& ParamName, bool Value)
{
	if (!IDisplayCluster::IsAvailable())
	{
		if (ParamName == TEXT("Active")) ApplyActive(Value);
		return;
	}
	FDisplayClusterClusterEventJson Event;
	Event.Name     = ClusterEventName;
	Event.Type     = TEXT("Param");
	Event.Category = TEXT("QNiagara");
	Event.bShouldDiscardOnRepeat = false;
	Event.Parameters.Add(TEXT("param"), ParamName);
	Event.Parameters.Add(TEXT("type"),  TEXT("bool"));
	Event.Parameters.Add(TEXT("v"),     Value ? TEXT("1") : TEXT("0"));
	IDisplayCluster::Get().GetClusterMgr()->EmitClusterEventJson(Event, false);
}

// ─── Listener + applier ───────────────────────────────────────────────────────

void AQNiagaraController::OnClusterEventJson(const FDisplayClusterClusterEventJson& Event)
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

void AQNiagaraController::ApplyFloat(const FString& ParamName, float Value)
{
	UNiagaraComponent* NC = ResolveNiagaraComponent();
	if (!NC)
	{
		UE_LOG(LogTemp, Warning, TEXT("[QNiagaraController] ApplyFloat skipped (no NC): %s=%.2f"), *ParamName, Value);
		return;
	}
	const FName Var(*FString::Printf(TEXT("User.%s"), *ParamName));
	NC->SetVariableFloat(Var, Value);
	UE_LOG(LogTemp, Verbose, TEXT("[QNiagaraController] %s = %.2f on '%s'"),
		*Var.ToString(), Value, *GetNameSafe(NC->GetOwner()));
}

void AQNiagaraController::ApplyColor(const FString& ParamName, const FLinearColor& Value)
{
	UNiagaraComponent* NC = ResolveNiagaraComponent();
	if (!NC) return;
	NC->SetVariableLinearColor(FName(*FString::Printf(TEXT("User.%s"), *ParamName)), Value);
}

void AQNiagaraController::ApplyActive(bool bNewActive)
{
	UNiagaraComponent* NC = ResolveNiagaraComponent();
	if (!NC)
	{
		UE_LOG(LogTemp, Warning, TEXT("[QNiagaraController] ApplyActive skipped (no NC): active=%d"), bNewActive ? 1 : 0);
		return;
	}
	if (bNewActive) NC->Activate();
	else            NC->Deactivate();
	UE_LOG(LogTemp, Log, TEXT("[QNiagaraController] Active = %s on '%s'"),
		bNewActive ? TEXT("ON") : TEXT("OFF"), *GetNameSafe(NC->GetOwner()));
}
