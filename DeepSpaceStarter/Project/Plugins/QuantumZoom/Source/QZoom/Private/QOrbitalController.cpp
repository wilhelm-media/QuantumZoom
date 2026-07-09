#include "QOrbitalController.h"
#include "NiagaraComponent.h"
#include "IDisplayCluster.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Blueprint/UserWidget.h"
#include "QOrbitalHUD.h"

const FString AQOrbitalController::ClusterEventName = TEXT("QOrbital.Param");

AQOrbitalController::AQOrbitalController()
{
	PrimaryActorTick.bCanEverTick = true;   // gamepad polling (primary node only; early-outs otherwise)
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
	ApplyFloat(TEXT("CurlPower"),      CurlPower);
	ApplyFloat(TEXT("CurlFrq"),        CurlFrq);
	ApplyFloat(TEXT("DensityShift"),   DensityShift);
	ApplyFloat(TEXT("ColorShift"),     ColorShift);
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
	BroadcastFloat(TEXT("CurlPower"),      CurlPower);
	BroadcastFloat(TEXT("CurlFrq"),        CurlFrq);
	BroadcastFloat(TEXT("DensityShift"),   DensityShift);
	BroadcastFloat(TEXT("ColorShift"),     ColorShift);
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
	else if (N == GET_MEMBER_NAME_CHECKED(AQOrbitalController, CurlPower))      BroadcastFloat(TEXT("CurlPower"),      CurlPower);
	else if (N == GET_MEMBER_NAME_CHECKED(AQOrbitalController, CurlFrq))        BroadcastFloat(TEXT("CurlFrq"),        CurlFrq);
	else if (N == GET_MEMBER_NAME_CHECKED(AQOrbitalController, DensityShift))   BroadcastFloat(TEXT("DensityShift"),   DensityShift);
	else if (N == GET_MEMBER_NAME_CHECKED(AQOrbitalController, ColorShift))     BroadcastFloat(TEXT("ColorShift"),     ColorShift);
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

// ─── Gamepad live control (primary node only) ──────────────────────────────────
// Hold Right Trigger = "Orbital Mode". B/X cycle the selected param, Left Stick
// adjusts it. All changes go through BroadcastFloat/BroadcastColor, so they sync
// to every cluster node exactly like the editor PostEditChange path.

void AQOrbitalController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bIsPrimary) return;             // only the operator's (primary) node reads the gamepad
	HandleGamepadInput(DeltaSeconds);
}

void AQOrbitalController::HandleGamepadInput(float Dt)
{
	UWorld* W = GetWorld();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	if (!PC) return;

	// Modifier: Right Trigger held (mirrors QZoomTest's Left-Trigger = free-flight; mutually exclusive)
	const bool bMode = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis) > 0.1f;
	if (!bMode)
	{
		bPrevNext = bPrevPrev = bPrevReset = false;   // clear edges so nothing queues while inactive
		if (OrbitalHUD) OrbitalHUD->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	// Cycle selected param: B = next, X = prev (edge-detected)
	const bool bNext = PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Right);
	const bool bPrev = PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Left);
	if (bNext && !bPrevNext) SelectedParam = (SelectedParam + 1) % NumParams();
	if (bPrev && !bPrevPrev) SelectedParam = (SelectedParam + NumParams() - 1) % NumParams();
	bPrevNext = bNext;
	bPrevPrev = bPrev;

	// Reset selected param to default: Right-Stick click (edge-detected)
	const bool bReset = PC->IsInputKeyDown(EKeys::Gamepad_RightThumbstick);
	if (bReset && !bPrevReset) ResetParamToDefault(SelectedParam);
	bPrevReset = bReset;

	// Adjust: Left Stick (deadzoned)
	const float DZ = 0.15f;
	float LX = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
	float LY = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
	LX = (FMath::Abs(LX) > DZ) ? LX : 0.f;
	LY = (FMath::Abs(LY) > DZ) ? LY : 0.f;

	if (SelectedParam == 10 || SelectedParam == 11)  // color params: X = hue, Y = brightness
	{
		if (LX != 0.f || LY != 0.f)
			NudgeColor(SelectedParam, LX * 360.f * GamepadAdjustRate * Dt, LY * GamepadAdjustRate * Dt);
	}
	else                                          // scalar params (0-9, 12, 13): X = value
	{
		if (LX != 0.f) NudgeScalar(SelectedParam, LX * Dt);
	}

	// Operator HUD — real Slate widget (NOT AddOnScreenDebugMessage, which the cluster
	// suppresses via DisableAllScreenMessages). Created lazily on the primary node.
	if (bShowGamepadHUD)
	{
		if (!OrbitalHUD)
		{
			OrbitalHUD = CreateWidget<UQOrbitalHUD>(PC, UQOrbitalHUD::StaticClass());
			if (OrbitalHUD) OrbitalHUD->AddToViewport(100);
		}
		if (OrbitalHUD)
		{
			OrbitalHUD->SetVisibility(ESlateVisibility::HitTestInvisible);
			OrbitalHUD->SetReadout(
				FString::Printf(TEXT("ORBITAL   %d / %d"), SelectedParam + 1, NumParams()),
				ParamDisplay(SelectedParam));
		}
	}
}

void AQOrbitalController::NudgeScalar(int32 Index, float NormDelta)
{
	float* P = nullptr; float Lo = 0.f, Hi = 1.f; const TCHAR* Name = TEXT("");
	switch (Index)
	{
		case 0: P = &Hybridization;  Lo = 0.f;  Hi = 3.f;     Name = TEXT("Hybridization");  break;
		case 1: P = &CloudFuzziness; Lo = 0.f;  Hi = 1.f;     Name = TEXT("CloudFuzziness"); break;
		case 2: P = &MaxRadius;      Lo = 50.f; Hi = 2000.f;  Name = TEXT("MaxRadius");      break;
		case 3: P = &LayerGradient;  Lo = 0.f;  Hi = 200.f;   Name = TEXT("LayerGradient");  break;
		case 4: P = &SwitchProgress; Lo = 0.f;  Hi = 1.f;     Name = TEXT("SwitchProgress"); break;
		case 5: P = &SpawnDensity;   Lo = 0.f;  Hi = 50000.f; Name = TEXT("SpawnDensity");   break;
		case 6: P = &ColorCoreEdge;  Lo = 0.f;  Hi = 1.f;     Name = TEXT("ColorCoreEdge");  break;
		case 7: P = &ColorEdgeEdge;  Lo = 0.f;  Hi = 1.f;     Name = TEXT("ColorEdgeEdge");  break;
		case 8: P = &CurlPower;      Lo = 0.f;  Hi = 20000.f; Name = TEXT("CurlPower");      break;
		case 9: P = &CurlFrq;        Lo = 0.f;  Hi = 5000.f;  Name = TEXT("CurlFrq");        break;
		case 12: P = &DensityShift;  Lo = 0.f;  Hi = 1.f;     Name = TEXT("DensityShift");   break;
		case 13: P = &ColorShift;    Lo = 0.f;  Hi = 1.f;     Name = TEXT("ColorShift");     break;
		default: return;
	}
	const float Range = Hi - Lo;
	*P = FMath::Clamp(*P + NormDelta * Range * GamepadAdjustRate, Lo, Hi);
	BroadcastFloat(Name, *P);
}

void AQOrbitalController::NudgeColor(int32 Index, float DeltaHueDeg, float DeltaValue)
{
	FLinearColor* C = (Index == 10) ? &CoreColor : (Index == 11) ? &EdgeColor : nullptr;
	if (!C) return;
	FLinearColor HSV = C->LinearRGBToHSV();                 // R = Hue(0-360), G = Sat, B = Value
	HSV.R = FMath::Fmod(HSV.R + DeltaHueDeg + 360.f, 360.f);
	HSV.B = FMath::Clamp(HSV.B + DeltaValue, 0.f, 1.f);
	const float SavedAlpha = C->A;
	*C = HSV.HSVToLinearRGB();
	C->A = SavedAlpha;                                      // preserve alpha (EdgeColor uses it)
	BroadcastColor(Index == 10 ? TEXT("CoreColor") : TEXT("EdgeColor"), *C);
}

FString AQOrbitalController::ParamDisplay(int32 Index) const
{
	switch (Index)
	{
		case 0: return FString::Printf(TEXT("Hybridization: %.2f"),  Hybridization);
		case 1: return FString::Printf(TEXT("CloudFuzziness: %.2f"), CloudFuzziness);
		case 2: return FString::Printf(TEXT("MaxRadius: %.0f"),      MaxRadius);
		case 3: return FString::Printf(TEXT("LayerGradient: %.1f"),  LayerGradient);
		case 4: return FString::Printf(TEXT("SwitchProgress: %.2f"), SwitchProgress);
		case 5: return FString::Printf(TEXT("SpawnDensity: %.0f"),   SpawnDensity);
		case 6: return FString::Printf(TEXT("ColorCoreEdge: %.2f"),  ColorCoreEdge);
		case 7: return FString::Printf(TEXT("ColorEdgeEdge: %.2f"),  ColorEdgeEdge);
		case 8: return FString::Printf(TEXT("CurlPower: %.0f"),      CurlPower);
		case 9: return FString::Printf(TEXT("CurlFrq: %.0f"),        CurlFrq);
		case 10: return FString::Printf(TEXT("CoreColor  R%.2f G%.2f B%.2f"), CoreColor.R, CoreColor.G, CoreColor.B);
		case 11: return FString::Printf(TEXT("EdgeColor  R%.2f G%.2f B%.2f"), EdgeColor.R, EdgeColor.G, EdgeColor.B);
		case 12: return FString::Printf(TEXT("DensityShift: %.2f  (S->O, data)"), DensityShift);
		case 13: return FString::Printf(TEXT("ColorShift: %.2f  (gold->blue, art)"), ColorShift);
	}
	return FString();
}

void AQOrbitalController::ResetParamToDefault(int32 Index)
{
	switch (Index)
	{
		case 0: Hybridization  = 3.f;    BroadcastFloat(TEXT("Hybridization"),  Hybridization);  break;
		case 1: CloudFuzziness = 0.7f;   BroadcastFloat(TEXT("CloudFuzziness"), CloudFuzziness); break;
		case 2: MaxRadius      = 250.f;  BroadcastFloat(TEXT("MaxRadius"),      MaxRadius);      break;
		case 3: LayerGradient  = 2.f;    BroadcastFloat(TEXT("LayerGradient"),  LayerGradient);  break;
		case 4: SwitchProgress = 0.f;    BroadcastFloat(TEXT("SwitchProgress"), SwitchProgress); break;
		case 5: SpawnDensity   = 8000.f; BroadcastFloat(TEXT("SpawnDensity"),   SpawnDensity);   break;
		case 6: ColorCoreEdge  = 0.4f;   BroadcastFloat(TEXT("ColorCoreEdge"),  ColorCoreEdge);  break;
		case 7: ColorEdgeEdge  = 0.9f;   BroadcastFloat(TEXT("ColorEdgeEdge"),  ColorEdgeEdge);  break;
		case 8: CurlPower      = 5000.f; BroadcastFloat(TEXT("CurlPower"),      CurlPower);      break;
		case 9: CurlFrq        = 1000.f; BroadcastFloat(TEXT("CurlFrq"),        CurlFrq);        break;
		case 10: CoreColor = FLinearColor(1.f, 0.4f, 0.05f, 1.f); BroadcastColor(TEXT("CoreColor"), CoreColor); break;
		case 11: EdgeColor = FLinearColor(0.3f, 0.75f, 1.f, 0.3f); BroadcastColor(TEXT("EdgeColor"), EdgeColor); break;
		case 12: DensityShift = 0.f; BroadcastFloat(TEXT("DensityShift"), DensityShift); break;
		case 13: ColorShift   = 0.f; BroadcastFloat(TEXT("ColorShift"),   ColorShift);   break;
	}
}
