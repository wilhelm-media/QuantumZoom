#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "QPerfWidget.generated.h"

class STextBlock;
class SVerticalBox;

struct FQNiagaraDisplay
{
	bool  bAvailable           = false;
	bool  bActive              = false;
	float VectorFieldIntensity = 0.f;
	float SpawnRate            = 0.f;
	float Lifetime             = 0.f;
	float SpriteSize           = 0.f;
	float EmitterRadius        = 0.f;
};

UCLASS()
class QZOOM_API UQPerfWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void UpdateStats(float Fps, float FtMs,
	                 float GameMs, float DrawMs, float RhiMs, float GpuMs,
	                 float TickMs, float ClusterLagMs,
	                 float RamMb,
	                 int32 GcEvents, float GcInterval, int32 ShaderJobs,
	                 int32 NiagaraActive, int32 NiagaraEmitters,
	                 int32 StuttersInWindow, float StutterIntervalAvg, bool bStutterThisFrame,
	                 bool bPharusRunning, int32 ActivePharusTracks,
	                 bool bTracking, float CountdownSecs,
	                 const FString& ExportMsg,
	                 int32 MenuPage, int32 MenuSelection,
	                 bool bExportEnabled, bool bPausePharus, bool bTestCubeActive,
	                 const FString& ExportPathDisplay,
	                 const FQNiagaraDisplay& Niagara,
	                 const FString& SoundDistStr,
	                 bool bRestartPromptActive, float RestartPromptRemaining);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	// Metric rows
	TSharedPtr<STextBlock> FpsText;
	TSharedPtr<STextBlock> GameText;
	TSharedPtr<STextBlock> DrawText;
	TSharedPtr<STextBlock> RhiText;
	TSharedPtr<STextBlock> GpuText;
	TSharedPtr<STextBlock> RamText;
	TSharedPtr<STextBlock> GcText;
	TSharedPtr<STextBlock> GcIntText;
	TSharedPtr<STextBlock> ShaderText;
	TSharedPtr<STextBlock> NiagaraText;
	TSharedPtr<STextBlock> StutterText;
	TSharedPtr<STextBlock> TickText;
	TSharedPtr<STextBlock> ClusterText;
	TSharedPtr<STextBlock> PharusStatusText;
	TSharedPtr<STextBlock> HeaderBadge;

	// Page indicator
	TSharedPtr<STextBlock> PageBadge;

	// Tracking page rows
	TSharedPtr<SVerticalBox> TrackingPageBox;
	TSharedPtr<STextBlock> ExportMenuText;
	TSharedPtr<STextBlock> ExportPathText;
	TSharedPtr<STextBlock> PharusToggleMenuText;
	TSharedPtr<STextBlock> PharusMenuText;
	TSharedPtr<STextBlock> StartTrackingMenuText;
	TSharedPtr<STextBlock> TestCubeMenuText;
	TSharedPtr<STextBlock> SoundDistText;
	TSharedPtr<STextBlock> RestartPromptText;

	// Niagara page rows
	TSharedPtr<SVerticalBox> NiagaraPageBox;
	TSharedPtr<STextBlock> NiaActiveText;
	TSharedPtr<STextBlock> NiaIntensityText;
	TSharedPtr<STextBlock> NiaSpawnText;
	TSharedPtr<STextBlock> NiaLifetimeText;
	TSharedPtr<STextBlock> NiaSpriteText;
	TSharedPtr<STextBlock> NiaRadiusText;

	// Bottom status row
	TSharedPtr<STextBlock> StatusText;
};
