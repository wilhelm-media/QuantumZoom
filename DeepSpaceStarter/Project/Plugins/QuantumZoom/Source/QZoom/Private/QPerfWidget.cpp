#include "QPerfWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> UQPerfWidget::RebuildWidget()
{
	const FSlateFontInfo Font     = FCoreStyle::GetDefaultFontStyle("Regular", 18);
	const FSlateFontInfo BoldFont = FCoreStyle::GetDefaultFontStyle("Bold", 20);

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

	// ── Restart prompt (only visible while active) ──────────────────────────
	Content->AddSlot()
	.AutoHeight()
	.HAlign(HAlign_Center)
	.Padding(FMargin(0.f, 0.f, 0.f, 8.f))
	[
		SAssignNew(RestartPromptText, STextBlock)
		.Font(BoldFont)
		.ColorAndOpacity(FLinearColor(1.f, 0.4f, 0.3f))
		.Visibility(EVisibility::Collapsed)
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
	AddMetricRow(TEXT("Game"),   GameText);
	AddMetricRow(TEXT("Draw"),   DrawText);
	AddMetricRow(TEXT("RHI"),    RhiText);
	AddMetricRow(TEXT("GPU"),    GpuText);
	AddMetricRow(TEXT("RAM"),    RamText);
	AddMetricRow(TEXT("GC"),       GcText);
	AddMetricRow(TEXT("GC Int"),   GcIntText);
	AddMetricRow(TEXT("Shader"),   ShaderText);
	AddMetricRow(TEXT("Niagara"),  NiagaraText);
	AddMetricRow(TEXT("Stutter"),  StutterText);
	AddMetricRow(TEXT("Tick"),     TickText);
	AddMetricRow(TEXT("Cluster"),  ClusterText);
	AddMetricRow(TEXT("Pharus"),   PharusStatusText);

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

	// ── Page badge (LB/RB switches pages) ─────────────────────────────────────
	Content->AddSlot()
	.AutoHeight()
	.Padding(FMargin(0.f, 0.f, 0.f, 4.f))
	[
		SAssignNew(PageBadge, STextBlock)
		.Font(BoldFont)
		.ColorAndOpacity(FLinearColor(0.55f, 0.75f, 1.f))
		.Text(FText::FromString(TEXT("◀ LB     TRACKING  [1/2]     RB ▶")))
	];

	// ── Tracking page ─────────────────────────────────────────────────────────
	Content->AddSlot()
	.AutoHeight()
	[
		SAssignNew(TrackingPageBox, SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 6.f))
		[
			SAssignNew(SoundDistText, STextBlock).Font(Font)
			.ColorAndOpacity(FLinearColor(0.55f, 0.85f, 1.f))
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 2.f))
		[
			SAssignNew(ExportMenuText, STextBlock).Font(Font)
			.ColorAndOpacity(FLinearColor(0.55f, 0.55f, 0.55f))
			.Text(FText::FromString(TEXT(">  EXPORT   OFF")))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(18.f, 0.f, 0.f, 2.f))
		[
			SAssignNew(ExportPathText, STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
			.ColorAndOpacity(FLinearColor(0.45f, 0.45f, 0.45f))
			.Visibility(EVisibility::Collapsed)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 2.f))
		[
			SAssignNew(PharusToggleMenuText, STextBlock).Font(Font)
			.ColorAndOpacity(FLinearColor(0.55f, 0.55f, 0.55f))
			.Text(FText::FromString(TEXT("   PHARUS   ○ OFF")))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 2.f))
		[
			SAssignNew(PharusMenuText, STextBlock).Font(Font)
			.ColorAndOpacity(FLinearColor(0.55f, 0.55f, 0.55f))
			.Text(FText::FromString(TEXT("   PHARUS   OFF")))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 2.f))
		[
			SAssignNew(StartTrackingMenuText, STextBlock).Font(BoldFont)
			.ColorAndOpacity(FLinearColor(0.55f, 0.55f, 0.55f))
			.Text(FText::FromString(TEXT("   START TRACKING")))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 2.f))
		[
			SAssignNew(TestCubeMenuText, STextBlock).Font(Font)
			.ColorAndOpacity(FLinearColor(0.55f, 0.55f, 0.55f))
			.Text(FText::FromString(TEXT("   TEST CUBE   OFF")))
		]
	];

	// ── Niagara page ──────────────────────────────────────────────────────────
	const FMargin RowPad(0.f, 2.f);
	const FLinearColor RowGrey(0.55f, 0.55f, 0.55f);

	Content->AddSlot().AutoHeight()
	[
		SAssignNew(NiagaraPageBox, SVerticalBox)
			.Visibility(EVisibility::Collapsed)
		+ SVerticalBox::Slot().AutoHeight().Padding(RowPad)
		[ SAssignNew(NiaActiveText,    STextBlock).Font(Font).ColorAndOpacity(RowGrey) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(RowPad)
		[ SAssignNew(NiaIntensityText, STextBlock).Font(Font).ColorAndOpacity(RowGrey) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(RowPad)
		[ SAssignNew(NiaSpawnText,     STextBlock).Font(Font).ColorAndOpacity(RowGrey) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(RowPad)
		[ SAssignNew(NiaLifetimeText,  STextBlock).Font(Font).ColorAndOpacity(RowGrey) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(RowPad)
		[ SAssignNew(NiaSpriteText,    STextBlock).Font(Font).ColorAndOpacity(RowGrey) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(RowPad)
		[ SAssignNew(NiaRadiusText,    STextBlock).Font(Font).ColorAndOpacity(RowGrey) ]
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

	TSharedRef<SBorder> Panel = SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox")))
		.BorderBackgroundColor(FLinearColor(0.04f, 0.04f, 0.04f, 0.88f))
		.Padding(FMargin(14.f))
		[Content];

	// Center horizontally and vertically in viewport
	return SNew(SVerticalBox)
	+ SVerticalBox::Slot()
	.FillHeight(1.f)
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		SNew(SBox).WidthOverride(720.f) [ Panel ]
	];
}

void UQPerfWidget::UpdateStats(float Fps, float FtMs,
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
                                bool bRestartPromptActive, float RestartPromptRemaining)
{
	// Page switching — show one page, hide the other
	if (TrackingPageBox.IsValid())
		TrackingPageBox->SetVisibility(MenuPage == 0 ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
	if (NiagaraPageBox.IsValid())
		NiagaraPageBox->SetVisibility(MenuPage == 1 ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
	if (PageBadge.IsValid())
	{
		const FString PageLabel = (MenuPage == 0) ? TEXT("TRACKING  [1/2]") : TEXT("NIAGARA  [2/2]");
		PageBadge->SetText(FText::FromString(FString::Printf(TEXT("◀ LB     %s     RB ▶"), *PageLabel)));
	}

	// Highlight thread times that exceed 16.7ms (60fps budget)
	auto ThreadColor = [](float Ms) -> FLinearColor
	{
		if (Ms > 16.7f) return FLinearColor(0.95f, 0.35f, 0.25f); // red — over budget
		if (Ms > 12.0f) return FLinearColor(0.95f, 0.80f, 0.30f); // amber — close
		return FLinearColor(0.88f, 0.88f, 0.88f);                  // normal
	};

	// Metrics
	if (FpsText.IsValid())
		FpsText->SetText(FText::FromString(FString::Printf(TEXT("%.1f fps  /  %.1f ms"), Fps, FtMs)));

	if (GameText.IsValid())
	{
		GameText->SetText(FText::FromString(FString::Printf(TEXT("%.1f ms"), GameMs)));
		GameText->SetColorAndOpacity(ThreadColor(GameMs));
	}

	if (DrawText.IsValid())
	{
		DrawText->SetText(FText::FromString(FString::Printf(TEXT("%.1f ms"), DrawMs)));
		DrawText->SetColorAndOpacity(ThreadColor(DrawMs));
	}

	if (RhiText.IsValid())
	{
		RhiText->SetText(FText::FromString(FString::Printf(TEXT("%.1f ms"), RhiMs)));
		RhiText->SetColorAndOpacity(ThreadColor(RhiMs));
	}

	if (GpuText.IsValid())
	{
		GpuText->SetText(FText::FromString(GpuMs > 0.f
			? FString::Printf(TEXT("%.1f ms"), GpuMs)
			: TEXT("N/A")));
		GpuText->SetColorAndOpacity(ThreadColor(GpuMs));
	}

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

	if (NiagaraText.IsValid())
		NiagaraText->SetText(FText::FromString(NiagaraActive > 0
			? FString::Printf(TEXT("%d active  /  %d emitters"), NiagaraActive, NiagaraEmitters)
			: TEXT("none active")));

	if (StutterText.IsValid())
	{
		const FString StutterStr = StutterIntervalAvg > 0.f
			? FString::Printf(TEXT("%d in 5s  ·  Δ %.2fs"), StuttersInWindow, StutterIntervalAvg)
			: FString::Printf(TEXT("%d in 5s"), StuttersInWindow);
		StutterText->SetText(FText::FromString(StutterStr));
		// Red while a stutter is happening this frame, amber if recent stutters, normal otherwise
		StutterText->SetColorAndOpacity(bStutterThisFrame
			? FLinearColor(0.95f, 0.25f, 0.25f)
			: (StuttersInWindow > 0 ? FLinearColor(0.95f, 0.80f, 0.30f) : FLinearColor(0.88f, 0.88f, 0.88f)));
	}

	if (TickText.IsValid())
	{
		TickText->SetText(FText::FromString(FString::Printf(TEXT("%.1f ms"), TickMs)));
		// Wall-clock tick: same thresholds as thread times — anything over 50 = stall
		TickText->SetColorAndOpacity(
			TickMs > 50.f  ? FLinearColor(0.95f, 0.25f, 0.25f) :
			TickMs > 20.f  ? FLinearColor(0.95f, 0.80f, 0.30f) :
			                 FLinearColor(0.88f, 0.88f, 0.88f));
	}

	if (ClusterText.IsValid())
	{
		ClusterText->SetText(FText::FromString(FString::Printf(TEXT("%.2f ms"), ClusterLagMs)));
		// Cluster lag: healthy < 5 ms, amber 5–50, red > 50 (sync stall)
		ClusterText->SetColorAndOpacity(
			ClusterLagMs > 50.f ? FLinearColor(0.95f, 0.25f, 0.25f) :
			ClusterLagMs >  5.f ? FLinearColor(0.95f, 0.80f, 0.30f) :
			                      FLinearColor(0.88f, 0.88f, 0.88f));
	}

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

	if (TestCubeMenuText.IsValid())
	{
		const bool bSel = MenuSelection == 4;
		TestCubeMenuText->SetText(FText::FromString(FString::Printf(
			TEXT("%s  TEST CUBE   %s"),
			bSel ? TEXT(">") : TEXT(" "),
			bTestCubeActive ? TEXT("ON") : TEXT("OFF"))));
		TestCubeMenuText->SetColorAndOpacity(bTestCubeActive ? OnOffColor(true) : MenuColor(bSel));
	}

	if (SoundDistText.IsValid())
	{
		SoundDistText->SetText(FText::FromString(SoundDistStr));
	}

	if (RestartPromptText.IsValid())
	{
		if (bRestartPromptActive)
		{
			RestartPromptText->SetText(FText::FromString(FString::Printf(
				TEXT("RESTART?   PRESS  B  TO CONFIRM   ( %.1fs )"),
				FMath::Max(0.f, RestartPromptRemaining))));
			RestartPromptText->SetVisibility(EVisibility::SelfHitTestInvisible);
		}
		else
		{
			RestartPromptText->SetVisibility(EVisibility::Collapsed);
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

	// ── Niagara page rendering ──────────────────────────────────────────────
	const int32 NiaSel = (MenuPage == 1) ? MenuSelection : -1;
	auto CursorAt = [NiaSel](int32 Idx) -> const TCHAR*
	{
		return NiaSel == Idx ? TEXT(">") : TEXT(" ");
	};

	if (NiaActiveText.IsValid())
	{
		const bool bSel = NiaSel == 0;
		const FString Line = !Niagara.bAvailable
			? FString(TEXT("   no QNiagaraController in level"))
			: FString::Printf(TEXT("%s  ACTIVE        %s"),
				CursorAt(0), Niagara.bActive ? TEXT("ON") : TEXT("OFF"));
		NiaActiveText->SetText(FText::FromString(Line));
		NiaActiveText->SetColorAndOpacity(Niagara.bActive
			? OnOffColor(true) : MenuColor(bSel));
	}

	auto RenderRow = [&](TSharedPtr<STextBlock>& Out, const TCHAR* Label, int32 Idx, const FString& ValueStr)
	{
		if (!Out.IsValid()) return;
		const bool bSel = NiaSel == Idx;
		Out->SetText(FText::FromString(FString::Printf(TEXT("%s  %-12s  %s"),
			CursorAt(Idx), Label, *ValueStr)));
		Out->SetColorAndOpacity(MenuColor(bSel));
	};

	RenderRow(NiaIntensityText, TEXT("INTENSITY"), 1, FString::Printf(TEXT("%.0f"),  Niagara.VectorFieldIntensity));
	RenderRow(NiaSpawnText,     TEXT("SPAWN"),     2, FString::Printf(TEXT("%.0f"),  Niagara.SpawnRate));
	RenderRow(NiaLifetimeText,  TEXT("LIFETIME"),  3, FString::Printf(TEXT("%.1fs"), Niagara.Lifetime));
	RenderRow(NiaSpriteText,    TEXT("SIZE"),      4, FString::Printf(TEXT("%.2f"),  Niagara.SpriteSize));
	RenderRow(NiaRadiusText,    TEXT("RADIUS"),    5, FString::Printf(TEXT("%.0f"),  Niagara.EmitterRadius));
}
