#include "QFloorOverlay.h"
#include "AefPharusSubsystem.h"
#include "AefPharusInstance.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

void UQFloorOverlay::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	CachedWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	const bool bNowRunning = [this]() -> bool
	{
		UGameInstance* GI = GetGameInstance();
		if (!GI) return false;
		UAefPharusSubsystem* Pharus = GI->GetSubsystem<UAefPharusSubsystem>();
		return Pharus && Pharus->IsPharusSystemRunning();
	}();

	// Pharus just stopped — clear tracks and allow rebind when it restarts
	if (bWasRunning && !bNowRunning)
	{
		Tracks.Empty();
		bDelegatesBound = false;
	}

	bWasRunning = bNowRunning;

	if (!bDelegatesBound && bNowRunning)
		TryBindDelegates();
}

void UQFloorOverlay::TryBindDelegates()
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) return;

	UAefPharusSubsystem* Pharus = GI->GetSubsystem<UAefPharusSubsystem>();
	if (!Pharus || !Pharus->IsPharusSystemRunning()) return;

	TArray<FName> Names = Pharus->GetAllInstanceNames();
	if (Names.Num() == 0) return;

	for (const FName& Name : Names)
	{
		if (UAefPharusInstance* Inst = Pharus->GetTrackerInstance(Name))
		{
			Inst->OnTrackUpdated.AddDynamic(this, &UQFloorOverlay::OnTrackUpdated);
			Inst->OnTrackLost.AddDynamic(this, &UQFloorOverlay::OnTrackLost);
		}
	}

	bDelegatesBound = true;
}

void UQFloorOverlay::OnTrackUpdated(int32 TrackID, const FAefPharusTrackData& TrackData)
{
	if (TrackData.bIsInsideBoundary)
		Tracks.Add(TrackID, FTrackInfo{ TrackData.RawPosition, CachedWorldTime });
	else
		Tracks.Remove(TrackID);
}

void UQFloorOverlay::OnTrackLost(int32 TrackID)
{
	Tracks.Remove(TrackID);
}

int32 UQFloorOverlay::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements,
		LayerId, InWidgetStyle, bParentEnabled);

	if (Tracks.Num() == 0)
		return LayerId + 3;

	const FVector2D ScreenSize = AllottedGeometry.GetLocalSize();
	const FSlateBrush* Brush   = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));

	for (const auto& Pair : Tracks)
	{
		const FTrackInfo& Info = Pair.Value;

		// Fade out over FadeTime seconds after last update
		const float Age   = CachedWorldTime - Info.LastSeenTime;
		const float Alpha = FMath::Clamp(1.f - Age / FadeTime, 0.f, 1.f);
		if (Alpha <= 0.f) continue;

		const float CX = Info.Position.X * ScreenSize.X;
		const float CY = Info.Position.Y * ScreenSize.Y;

		// Outer glow
		constexpr float GlowR = 36.f;
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(
				FVector2D(GlowR * 2.f, GlowR * 2.f),
				FSlateLayoutTransform(FVector2D(CX - GlowR, CY - GlowR))),
			Brush, ESlateDrawEffect::None,
			FLinearColor(0.f, 0.75f, 1.f, 0.15f * Alpha));

		// Mid ring
		constexpr float MidR = 20.f;
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
			AllottedGeometry.ToPaintGeometry(
				FVector2D(MidR * 2.f, MidR * 2.f),
				FSlateLayoutTransform(FVector2D(CX - MidR, CY - MidR))),
			Brush, ESlateDrawEffect::None,
			FLinearColor(0.f, 0.88f, 1.f, 0.35f * Alpha));

		// Center dot
		constexpr float DotR = 7.f;
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
			AllottedGeometry.ToPaintGeometry(
				FVector2D(DotR * 2.f, DotR * 2.f),
				FSlateLayoutTransform(FVector2D(CX - DotR, CY - DotR))),
			Brush, ESlateDrawEffect::None,
			FLinearColor(0.8f, 1.f, 1.f, 0.95f * Alpha));
	}

	return LayerId + 3;
}
