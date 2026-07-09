#include "QZoomRailPawn.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "IDisplayCluster.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Engine/World.h"
#include "Curves/CurveFloat.h"
#include "Components/TextRenderComponent.h"

const FString AQZoomRailPawn::EventName = TEXT("QZoomRail.State");

AQZoomRailPawn::AQZoomRailPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);

	// Constant scale/zoom panel — a real 3D element rigidly parented to the camera (stereo-safe,
	// sits at a comfortable depth). Faces back toward the camera (TextRender front is +X).
	Readout = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Readout"));
	Readout->SetupAttachment(Camera);
	Readout->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));
	Readout->SetHorizontalAlignment(EHTA_Center);
	Readout->SetVerticalAlignment(EVRTA_TextCenter);
	Readout->SetTextRenderColor(FColor(190, 225, 255));

	// PIE convenience so the pawn IS the view when you hit Play. In the cluster the view comes
	// from the DCRA — which this pawn drives via DisplayClusterRoot (or by being its parent).
	AutoPossessPlayer = EAutoReceiveInput::Player0;
}

void AQZoomRailPawn::BeginPlay()
{
	Super::BeginPlay();

	// Role. In desktop PIE / editor the DisplayCluster module is "available" but there is no live
	// cluster session, so IsPrimary() can report false and silently kill input. Gate on the OPERATION
	// MODE instead — only a real nDisplay launch runs in Cluster mode.
	bInCluster = IDisplayCluster::IsAvailable()
	          && IDisplayCluster::Get().GetOperationMode() == EDisplayClusterOperationMode::Cluster;
	if (bInCluster)
	{
		bIsPrimary = IDisplayCluster::Get().GetClusterMgr()->IsPrimary();
		ClusterListener = FOnClusterEventJsonListener::CreateUObject(this, &AQZoomRailPawn::OnClusterEvent);
		IDisplayCluster::Get().GetClusterMgr()->AddClusterEventJsonListener(ClusterListener);
	}
	else
	{
		bIsPrimary = true;   // PIE / editor / standalone → drive input + apply directly
	}
	UE_LOG(LogTemp, Log, TEXT("[QZoomRailPawn] cluster=%d primary=%d"), bInCluster, bIsPrimary);

	// Guarantee we are the view even if the level's GameMode spawned its own default pawn.
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		if (PC->GetPawn() != this) PC->Possess(this);
	}

	if (Readout)
	{
		Readout->SetRelativeLocation(ReadoutOffset);
		Readout->SetWorldSize(ReadoutSize);
	}

	if (AActor* D = DisplayClusterRoot.LoadSynchronous()) ResolvedDCRA = D;

	ApplyState();
}

void AQZoomRailPawn::EndPlay(const EEndPlayReason::Type Reason)
{
	if (bInCluster)
		IDisplayCluster::Get().GetClusterMgr()->RemoveClusterEventJsonListener(ClusterListener);
	Super::EndPlay(Reason);
}

void AQZoomRailPawn::Tick(float Dt)
{
	Super::Tick(Dt);
	if (!bIsPrimary) return;    // only the operator's node reads the gamepad; secondaries apply via the event
	PollGamepad(Dt);
	Broadcast();
	ApplyState();               // primary applies immediately too (don't wait to hear our own event back)
}

void AQZoomRailPawn::PollGamepad(float Dt)
{
	UWorld* W = GetWorld();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	if (!PC) return;

	// Zoom: Right Trigger in, Left Trigger out. CINEMATIC — the trigger sets a TARGET speed
	// (shaped by the along-rail profile: slow at stations, fast in the gaps); the actual speed
	// eases toward it (SpeedDamping) so starts/stops glide instead of snapping.
	float RT = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis);
	float LT = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftTriggerAxis);
	// keyboard fallback so the zoom is testable on a desktop without a gamepad
	if (PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::Up))   RT = FMath::Max(RT, 1.f);
	if (PC->IsInputKeyDown(EKeys::S) || PC->IsInputKeyDown(EKeys::Down)) LT = FMath::Max(LT, 1.f);
	const float Drive = RT - LT;                                             // -1..1 operator intent
	const float TargetVel = Drive * BaseZoomRate * SpeedProfileAt(ZoomProgress);
	ZoomVel = FMath::FInterpTo(ZoomVel, TargetVel, Dt, SpeedDamping);        // eased accel/decel
	ZoomProgress = FMath::Clamp(ZoomProgress + ZoomVel * Dt, 0.f, 1.f);

	// Look: Right Stick (deadzoned), applied as an offset over rail-forward
	const float DZ = 0.15f;
	float RX = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX);
	float RY = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightY);
	RX = (FMath::Abs(RX) > DZ) ? RX : 0.f;
	RY = (FMath::Abs(RY) > DZ) ? RY : 0.f;

	if (RX != 0.f || RY != 0.f)
	{
		LookYaw   += RX * LookRate * Dt;
		LookPitch  = FMath::Clamp(LookPitch + RY * LookRate * Dt, -MaxPitch, MaxPitch);
	}
	else if (bRecenterLook)
	{
		LookYaw   = FMath::FInterpTo(LookYaw,   0.f, Dt, 2.f);
		LookPitch = FMath::FInterpTo(LookPitch, 0.f, Dt, 2.f);
	}
}

void AQZoomRailPawn::Broadcast()
{
	if (!bInCluster) return;   // PIE/standalone: primary already applied in Tick
	FDisplayClusterClusterEventJson Event;
	Event.Name     = EventName;
	Event.Type     = TEXT("State");
	Event.Category = TEXT("QZoomRail");
	Event.bShouldDiscardOnRepeat = false;
	Event.Parameters.Add(TEXT("z"),     FString::SanitizeFloat(ZoomProgress));
	Event.Parameters.Add(TEXT("yaw"),   FString::SanitizeFloat(LookYaw));
	Event.Parameters.Add(TEXT("pitch"), FString::SanitizeFloat(LookPitch));
	IDisplayCluster::Get().GetClusterMgr()->EmitClusterEventJson(Event, false);
}

void AQZoomRailPawn::OnClusterEvent(const FDisplayClusterClusterEventJson& E)
{
	if (E.Name != EventName) return;
	ZoomProgress = FCString::Atof(*E.Parameters.FindRef(TEXT("z")));
	LookYaw      = FCString::Atof(*E.Parameters.FindRef(TEXT("yaw")));
	LookPitch    = FCString::Atof(*E.Parameters.FindRef(TEXT("pitch")));
	ApplyState();
}

FVector AQZoomRailPawn::EvalRail(float T) const
{
	T = FMath::Clamp(T, 0.f, 1.f);
	if (Waypoints.Num() >= 2)
	{
		const float Seg = T * (float)(Waypoints.Num() - 1);
		const int32 I   = FMath::Clamp((int32)Seg, 0, Waypoints.Num() - 2);
		return FMath::Lerp(Waypoints[I], Waypoints[I + 1], Seg - (float)I);
	}
	return FMath::Lerp(RailStart, RailEnd, T);
}

float AQZoomRailPawn::SpeedProfileAt(float T) const
{
	// Authored curve wins if present (full cinematic control).
	if (SpeedProfile) return FMath::Max(0.f, SpeedProfile->GetFloatValue(T));

	// Fallback: analytic dwell — speed dips to StationDwell within DwellWidth of each station,
	// smoothstepping back to full speed in the gaps. Gives cinematic slow-at-detail out of the box.
	if (StationCount < 2) return 1.f;
	float Nearest = BIG_NUMBER;
	for (int32 i = 0; i < StationCount; ++i)
	{
		const float S = (float)i / (float)(StationCount - 1);
		Nearest = FMath::Min(Nearest, FMath::Abs(T - S));
	}
	const float A = FMath::Clamp(Nearest / FMath::Max(DwellWidth, KINDA_SMALL_NUMBER), 0.f, 1.f);
	const float Smooth = A * A * (3.f - 2.f * A);   // smoothstep
	return FMath::Lerp(StationDwell, 1.f, Smooth);
}

void AQZoomRailPawn::ApplyState()
{
	const FVector Pos = EvalRail(ZoomProgress);

	// Base orientation = rail-forward (finite difference), plus the look-around offset.
	const FVector Fwd = EvalRail(FMath::Min(ZoomProgress + 0.002f, 1.f))
	                  - EvalRail(FMath::Max(ZoomProgress - 0.002f, 0.f));
	const FRotator Base = Fwd.IsNearlyZero() ? FRotator::ZeroRotator : Fwd.Rotation();
	const FRotator Rot(Base.Pitch + LookPitch, Base.Yaw + LookYaw, 0.f);

	SetActorLocationAndRotation(Pos, Rot);

	// Drive the nDisplay root too, if one was assigned (so the cluster view follows the rail
	// without re-parenting the DCRA). Harmless double-set if the DCRA is already our child.
	if (AActor* D = ResolvedDCRA.Get())
		D->SetActorLocationAndRotation(Pos, Rot);

	UpdateReadout();
}

float AQZoomRailPawn::CurrentScaleMeters() const
{
	const int32 N = ScaleMeters.Num();
	if (N == 0) return 0.f;
	if (N == 1) return ScaleMeters[0];
	const float P = FMath::Clamp(ZoomProgress, 0.f, 1.f) * (float)(N - 1);
	const int32 I = FMath::Clamp((int32)P, 0, N - 2);
	const float F = P - (float)I;
	const float A = FMath::Loge(FMath::Max(ScaleMeters[I],     1e-30f));
	const float B = FMath::Loge(FMath::Max(ScaleMeters[I + 1], 1e-30f));
	return FMath::Exp(FMath::Lerp(A, B, F));   // log-space interpolation across the scale gulf
}

FString AQZoomRailPawn::FormatScale(float M) const
{
	struct FUnit { float S; const TCHAR* U; };
	static const FUnit Units[] = {
		{1e-15f, TEXT("fm")}, {1e-12f, TEXT("pm")}, {1e-10f, TEXT("A")},
		{1e-9f,  TEXT("nm")}, {1e-6f,  TEXT("um")}, {1e-3f,  TEXT("mm")},
		{1e-2f,  TEXT("cm")}, {1.f,    TEXT("m")} };
	int32 Pick = 0;
	for (int32 i = 0; i < 8; ++i) if (Units[i].S <= M) Pick = i;
	return FString::Printf(TEXT("%.2f %s"), M / Units[Pick].S, Units[Pick].U);
}

void AQZoomRailPawn::UpdateReadout()
{
	if (!Readout) return;
	const float M     = CurrentScaleMeters();
	const float Ref   = (ScaleMeters.Num() > 0) ? ScaleMeters[0] : 1.f;
	const float Power = FMath::LogX(10.f, FMath::Max(Ref, 1e-30f) / FMath::Max(M, 1e-30f));
	const int32 Bars  = FMath::Clamp(FMath::RoundToInt(ZoomProgress * 16.f), 0, 16);
	const FString Bar = FString::ChrN(Bars, TEXT('|')) + FString::ChrN(16 - Bars, TEXT('.'));
	Readout->SetText(FText::FromString(FString::Printf(
		TEXT("SCALE  %s\nZOOM   x10^%.1f\n[%s] %.0f%%"),
		*FormatScale(M), Power, *Bar, ZoomProgress * 100.f)));
}
