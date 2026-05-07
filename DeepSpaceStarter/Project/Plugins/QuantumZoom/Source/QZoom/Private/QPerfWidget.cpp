#include "QPerfWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> UQPerfWidget::RebuildWidget()
{
	const FSlateFontInfo Font     = FCoreStyle::GetDefaultFontStyle("Regular", 11);
	const FSlateFontInfo BoldFont = FCoreStyle::GetDefaultFontStyle("Bold", 11);

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);

	// ── Header ───────────────────────────────────────────────────────────────
	Content->AddSlot()
	.AutoHeight()
	.Padding(FMargin(0.f, 0.f, 0.f, 8.f))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			SNew(STextBlock)
			.Font(BoldFont)
			.ColorAndOpacity(FLinearColor::White)
			.Text(FText::FromString(TEXT("PERFORMANCE MONITOR")))
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SAssignNew(HeaderBadge, STextBlock)
			.Font(Font)
			.ColorAndOpacity(FLinearColor::Transparent)
		]
	];

	// ── Metric rows ──────────────────────────────────────────────────────────
	auto AddMetricRow = [&](const TCHAR* Label, TSharedPtr<STextBlock>& OutText)
	{
		Content->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.f, 2.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.28f)
			[
				SNew(STextBlock)
				.Font(Font)
				.ColorAndOpacity(FLinearColor(0.45f, 0.45f, 0.45f))
				.Text(FText::FromString(Label))
			]
			+ SHorizontalBox::Slot().FillWidth(0.72f)
			[
				SAssignNew(OutText, STextBlock)
				.Font(Font)
				.ColorAndOpacity(FLinearColor(0.88f, 0.88f, 0.88f))
			]
		];
	};

	AddMetricRow(TEXT("FPS"),    FpsText);
	AddMetricRow(TEXT("GPU"),    GpuText);
	AddMetricRow(TEXT("RAM"),    RamText);
	AddMetricRow(TEXT("GC"),     GcText);
	AddMetricRow(TEXT("GC Int"), GcIntText);
	AddMetricRow(TEXT("Shader"), ShaderText);
	AddMetricRow(TEXT("Pharus"), PharusStatusText);

	auto AddDivider = [&]()
	{
		Content->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.f, 7.f, 0.f, 5.f))
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")))
			.BorderBackgroundColor(FLinearColor(0.15f, 0.15f, 0.15f))
			.Padding(FMargin(0.f, 1.f))
			[SNew(SSpacer)]
		];
	};

	// ── Divider ───────────────────────────────────────────────────────────────
	AddDivider();

	// ── Menu rows (D-Pad navigable) ───────────────────────────────────────────
	Content->AddSlot()
	.AutoHeight()
	.Padding(FMargin(0.f, 2.f))
	[
		SAssignNew(ExportMenuText, STextBlock)
		.Font(Font)
		.ColorAndOpacity(FLinearColor(0.55f, 0.55f, 0.55f))
		.Text(FText::FromString(TEXT(">  EXPORT   OFF")))
	];

	Content->AddSlot()
	.AutoHeight()
	.Padding(FMargin(18.f, 0.f, 0.f, 2.f))
	[
		SAssignNew(ExportPathText, STextBlock)
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
		.ColorAndOpacity(FLinearColor(0.45f, 0.45f, 0.45f))
		.Visibility(EVisibility::Collapsed)
	];

	Content->AddSlot()
	.AutoHeight()
	.Padding(FMargin(0.f, 2.f))
	[
		SAssignNew(PharusToggleMenuText, STextBlock)
		.Font(Font)
		.ColorAndOpacity(FLinearColor(0.55f, 0.55f, 0.55f))
		.Text(FText::FromString(TEXT("   PHARUS   ○ OFF")))
	];

	Content->AddSlot()
	.AutoHeight()
	.Padding(FMargin(0.f, 2.f))
	[
		SAssignNew(PharusMenuText, STextBlock)
		.Font(Font)
		.ColorAndOpacity(FLinearColor(0.55f, 0.55f, 0.55f))
		.Text(FText::FromString(TEXT("   PHARUS   OFF")))
	];

	Content->AddSlot()
	.AutoHeight()
	.Padding(FMargin(0.f, 4.f, 0.f, 2.f))
	[
		SAssignNew(StartTrackingMenuText, STextBlock)
		.Font(BoldFont)
		.ColorAndOpacity(FLinearColor(0.55f, 0.55f, 0.55f))
		.Text(FText::FromString(TEXT("   START TRACKING")))
	];

	// ── Divider ───────────────────────────────────────────────────────────────
	AddDivider();

	// ── Status row — export result only ──────────────────────────────────────
	Content->AddSlot()
	.AutoHeight()
	[
		SAssignNew(StatusText, STextBlock)
		.Font(Font)
		.ColorAndOpacity(FLinearColor(0.2f, 0.85f, 0.3f))
		.Visibility(EVisibility::Collapsed)
	];

	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")))
		.BorderBackgroundColor(FLinearColor(0.04f, 0.04f, 0.04f, 0.88f))
		.Padding(FMargin(14.f))
		[Content];
}

void UQPerfWidget::UpdateStats(float Fps, float FtMs, float GpuMs, float RamMb,
                                int32 GcEvents, float GcInterval, int32 ShaderJobs,
                                bool bPharusRunning, int32 ActivePharusTracks,
                                bool bTracking, float CountdownSecs,
                                const FString& ExportMsg,
                                int32 MenuSelection, bool bExportEnabled, bool bPausePharus,
                                const FString& ExportPathDisplay)
{
	// Metrics
	if (FpsText.IsValid())
		FpsText->SetText(FText::FromString(FString::Printf(TEXT("%.1f fps  /  %.1f ms"), Fps, FtMs)));

	if (GpuText.IsValid())
		GpuText->SetText(FText::FromString(GpuMs > 0.f
			? FString::Printf(TEXT("%.1f ms"), GpuMs)
			: TEXT("N/A")));

	if (RamText.IsValid())
		RamText->SetText(FText::FromString(FString::Printf(TEXT("%.0f MB"), RamMb)));

	if (GcText.IsValid())
		GcText->SetText(FText::FromString(GcInterval > 0.f
			? FString::Printf(TEXT("%d events   last: %.2fs"), GcEvents, GcInterval)
			: FString::Printf(TEXT("%d events"), GcEvents)));

	if (GcIntText.IsValid())
		GcIntText->SetText(FText::FromString(GcInterval > 0.f
			? FString::Printf(TEXT("delta %.2fs"), GcInterval)
			: TEXT("--")));

	if (ShaderText.IsValid())
		ShaderText->SetText(FText::FromString(FString::Printf(TEXT("%d pending"), ShaderJobs)));

	if (PharusStatusText.IsValid())
	{
		if (bPharusRunning)
		{
			PharusStatusText->SetText(FText::FromString(
				FString::Printf(TEXT("● RUNNING  (%d tracks)"), ActivePharusTracks)));
			PharusStatusText->SetColorAndOpacity(FLinearColor(0.2f, 0.85f, 0.3f));
		}
		else
		{
			PharusStatusText->SetText(FText::FromString(TEXT("○ STOPPED")));
			PharusStatusText->SetColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f));
		}
	}

	if (HeaderBadge.IsValid())
	{
		HeaderBadge->SetText(FText::FromString(bTracking ? TEXT("[● REC]") : TEXT("")));
		HeaderBadge->SetColorAndOpacity(bTracking
			? FLinearColor(0.9f, 0.2f, 0.2f)
			: FLinearColor::Transparent);
	}

	// Menu rows
	auto MenuColor = [](bool bSelected) -> FLinearColor
	{
		return bSelected ? FLinearColor::White : FLinearColor(0.50f, 0.50f, 0.50f);
	};
	auto OnOffColor = [](bool bOn) -> FLinearColor
	{
		return bOn ? FLinearColor(0.2f, 0.9f, 0.35f) : FLinearColor(0.55f, 0.55f, 0.55f);
	};

	if (ExportMenuText.IsValid())
	{
		const bool bSel = MenuSelection == 0;
		ExportMenuText->SetText(FText::FromString(FString::Printf(
			TEXT("%s  EXPORT   %s"), bSel ? TEXT(">") : TEXT(" "),
			bExportEnabled ? TEXT("ON") : TEXT("OFF"))));
		ExportMenuText->SetColorAndOpacity(bExportEnabled ? OnOffColor(true) : MenuColor(bSel));
	}

	if (ExportPathText.IsValid())
	{
		if (bExportEnabled && !ExportPathDisplay.IsEmpty())
		{
			ExportPathText->SetText(FText::FromString(ExportPathDisplay));
			ExportPathText->SetVisibility(EVisibility::SelfHitTestInvisible);
		}
		else
		{
			ExportPathText->SetVisibility(EVisibility::Collapsed);
		}
	}

	if (PharusToggleMenuText.IsValid())
	{
		const bool bSel = MenuSelection == 1;
		if (bPharusRunning)
		{
			PharusToggleMenuText->SetText(FText::FromString(FString::Printf(
				TEXT("%s  PHARUS   ● RUNNING  (%d)"), bSel ? TEXT(">") : TEXT(" "), ActivePharusTracks)));
			PharusToggleMenuText->SetColorAndOpacity(bSel ? FLinearColor::White : FLinearColor(0.2f, 0.85f, 0.3f));
		}
		else
		{
			PharusToggleMenuText->SetText(FText::FromString(FString::Printf(
				TEXT("%s  PHARUS   ○ STOPPED"), bSel ? TEXT(">") : TEXT(" "))));
			PharusToggleMenuText->SetColorAndOpacity(MenuColor(bSel));
		}
	}

	if (PharusMenuText.IsValid())
	{
		const bool bSel = MenuSelection == 2;
		PharusMenuText->SetText(FText::FromString(FString::Printf(
			TEXT("%s  TRACK PHARUS   %s"), bSel ? TEXT(">") : TEXT(" "),
			bPausePharus ? TEXT("NO") : TEXT("YES"))));
		PharusMenuText->SetColorAndOpacity(!bPausePharus ? OnOffColor(true) : MenuColor(bSel));
	}

	if (StartTrackingMenuText.IsValid())
	{
		const bool bSel = MenuSelection == 3;
		if (bTracking)
		{
			const int32 Mins = FMath::FloorToInt(CountdownSecs / 60.f);
			const int32 Secs = FMath::FloorToInt(FMath::Fmod(CountdownSecs, 60.f));
			StartTrackingMenuText->SetText(FText::FromString(
				FString::Printf(TEXT("  ● TRACKING  %02d:%02d"), Mins, Secs)));
			StartTrackingMenuText->SetColorAndOpacity(FLinearColor(0.9f, 0.2f, 0.2f));
		}
		else
		{
			StartTrackingMenuText->SetText(FText::FromString(
				FString::Printf(TEXT("%s  START TRACKING"), bSel ? TEXT(">") : TEXT(" "))));
			StartTrackingMenuText->SetColorAndOpacity(MenuColor(bSel));
		}
	}

	// Status row — countdown during tracking, export result after, empty otherwise
	if (StatusText.IsValid())
	{
		if (!ExportMsg.IsEmpty() && !bTracking)
		{
			StatusText->SetText(FText::FromString(ExportMsg));
			StatusText->SetColorAndOpacity(FLinearColor(0.2f, 0.85f, 0.3f));
			StatusText->SetVisibility(EVisibility::SelfHitTestInvisible);
		}
		else
		{
			StatusText->SetVisibility(EVisibility::Collapsed);
		}
	}
}
