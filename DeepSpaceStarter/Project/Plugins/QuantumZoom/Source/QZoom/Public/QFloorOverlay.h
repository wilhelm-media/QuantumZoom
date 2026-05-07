#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AefPharusTypes.h"
#include "QFloorOverlay.generated.h"

UCLASS()
class QZOOM_API UQFloorOverlay : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	UFUNCTION()
	void OnTrackUpdated(int32 TrackID, const FAefPharusTrackData& TrackData);

	UFUNCTION()
	void OnTrackLost(int32 TrackID);

private:
	void TryBindDelegates();

	struct FTrackInfo
	{
		FVector2D Position;
		float     LastSeenTime = 0.f;
	};

	TMap<int32, FTrackInfo> Tracks;
	bool  bDelegatesBound = false;
	bool  bWasRunning     = false;
	float CachedWorldTime = 0.f;

	static constexpr float FadeTime = 1.5f;
};
