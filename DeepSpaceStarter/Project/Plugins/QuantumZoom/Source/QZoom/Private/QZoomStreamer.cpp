#include "QZoomStreamer.h"
#include "QZoomStagePawn.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "EngineUtils.h"

AZoomStreamer::AZoomStreamer()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval  = 0.1f;   // 10 Hz is plenty for streaming decisions
}

void AZoomStreamer::BeginPlay()
{
	Super::BeginPlay();
	LoadedState.Init(-1, StageLevels.Num());
	ResolvePawn();
}

AQZoomStagePawn* AZoomStreamer::ResolvePawn()
{
	if (Pawn.IsValid()) return Pawn.Get();
	if (AQZoomStagePawn* P = StagePawn.LoadSynchronous()) { Pawn = P; return P; }
	if (UWorld* W = GetWorld())
	{
		for (TActorIterator<AQZoomStagePawn> It(W); It; ++It) { Pawn = *It; return *It; }
	}
	return nullptr;
}

void AZoomStreamer::Tick(float Dt)
{
	Super::Tick(Dt);
	UpdateStreaming();
}

void AZoomStreamer::UpdateStreaming()
{
	const int32 N = StageLevels.Num();
	if (N == 0) return;

	AQZoomStagePawn* P = ResolvePawn();
	if (!P) return;

	const float Pos  = P->GetZoomProgress() * (float)(N - 1);        // fractional stage index
	const int32 Cur  = FMath::Clamp(FMath::RoundToInt(Pos), 0, N - 1);
	const int32 Lead = FMath::Clamp(FMath::FloorToInt(Pos), 0, N - 1); // block we are moving out of

	for (int32 i = 0; i < N; ++i)
	{
		bool bWant = (i >= Cur - LoadBehind) && (i <= Cur + LoadAhead);

		// Predictive lead-load: once >PredictiveLead into block 'Lead', pre-load the one beyond the window.
		if (!bWant && i == Lead + LoadAhead + 1 && (Pos - (float)Lead) > PredictiveLead)
			bWant = true;

		SetStageLoaded(i, bWant);
	}
}

void AZoomStreamer::SetStageLoaded(int32 Index, bool bLoad)
{
	if (!StageLevels.IsValidIndex(Index)) return;

	const int32 Want = bLoad ? 1 : 0;
	if (LoadedState.IsValidIndex(Index) && LoadedState[Index] == Want) return;   // no state change → skip

	ULevelStreaming* LS = UGameplayStatics::GetStreamingLevel(this, StageLevels[Index]);
	if (!LS)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ZoomStreamer] Streaming level '%s' not found in persistent level — add it as a sublevel."),
			*StageLevels[Index].ToString());
		LoadedState[Index] = Want;   // record it so we don't spam the warning every tick
		return;
	}

	LS->SetShouldBeLoaded(bLoad);
	LS->SetShouldBeVisible(bLoad);
	LoadedState[Index] = Want;
	UE_LOG(LogTemp, Log, TEXT("[ZoomStreamer] %s -> %s"), *StageLevels[Index].ToString(), bLoad ? TEXT("LOAD") : TEXT("UNLOAD"));
}
