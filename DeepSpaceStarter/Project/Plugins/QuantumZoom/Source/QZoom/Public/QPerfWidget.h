#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "QPerfWidget.generated.h"

class STextBlock;

UCLASS()
class QZOOM_API UQPerfWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void UpdateStats(float Fps, float FtMs, float GpuMs, float RamMb,
	                 int32 GcEvents, float GcInterval, int32 ShaderJobs,
	                 bool bPharusRunning, int32 ActivePharusTracks,
	                 bool bTracking, float CountdownSecs,
	                 const FString& ExportMsg,
	                 int32 MenuSelection, bool bExportEnabled, bool bPausePharus,
	                 const FString& ExportPathDisplay);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	// Metric rows
	TSharedPtr<STextBlock> FpsText;
	TSharedPtr<STextBlock> GpuText;
	TSharedPtr<STextBlock> RamText;
	TSharedPtr<STextBlock> GcText;
	TSharedPtr<STextBlock> GcIntText;
	TSharedPtr<STextBlock> ShaderText;
	TSharedPtr<STextBlock> PharusStatusText;
	TSharedPtr<STextBlock> HeaderBadge;

	// Menu rows (D-Pad navigable)
	TSharedPtr<STextBlock> ExportMenuText;
	TSharedPtr<STextBlock> ExportPathText;
	TSharedPtr<STextBlock> PharusToggleMenuText;
	TSharedPtr<STextBlock> PharusMenuText;
	TSharedPtr<STextBlock> StartTrackingMenuText;

	// Bottom status row
	TSharedPtr<STextBlock> StatusText;
};
