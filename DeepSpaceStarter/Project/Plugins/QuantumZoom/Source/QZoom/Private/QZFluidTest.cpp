#include "QZFluidTest.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"

AQZFluidTest::AQZFluidTest()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Particles = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Particles"));
	Particles->SetupAttachment(RootComponent);
	Particles->SetMobility(EComponentMobility::Movable);
	Particles->SetCastShadow(false);
	Particles->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Thousands of instances updated every frame: culling and shadows are pure cost here.
	Particles->SetCullDistances(0, 0);

	Obstacle = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Obstacle"));
	Obstacle->SetupAttachment(RootComponent);
	Obstacle->SetMobility(EComponentMobility::Movable);
	Obstacle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

bool AQZFluidTest::ReadField()
{
	if (!FluidTexture)
	{
		UE_LOG(LogTemp, Warning, TEXT("[QZFluid] no FluidTexture assigned"));
		return false;
	}
	// NeverStream + no mips is what makes mip 0 reliably present on the CPU. Without it the
	// texture may be streamed out and the lock hands back nothing, which looks identical to a
	// bad import.
	FluidTexture->NeverStream = true;
	FTexturePlatformData* PD = FluidTexture->GetPlatformData();
	if (!PD || PD->Mips.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[QZFluid] texture has no platform data / mips"));
		return false;
	}
	FTexture2DMipMap& Mip = PD->Mips[0];
	FieldW = Mip.SizeX;
	FieldH = Mip.SizeY;
	const void* Raw = Mip.BulkData.LockReadOnly();
	if (!Raw)
	{
		Mip.BulkData.Unlock();
		UE_LOG(LogTemp, Warning, TEXT("[QZFluid] could not lock mip 0 — is the texture compressed?"));
		return false;
	}
	const int32 Bytes = FieldW * FieldH * 4;
	Field.SetNumUninitialized(Bytes);
	FMemory::Memcpy(Field.GetData(), Raw, Bytes);
	Mip.BulkData.Unlock();

	UE_LOG(LogTemp, Warning,
		TEXT("[QZFluid] field %dx%d, format %d (BGRA byte order assumed), %d frames of %d px"),
		FieldW, FieldH, (int32)PD->PixelFormat, FrameCount, GridN);
	return true;
}

/** Bilinear sample inside the tile for Frame. u,v are 0..1 across the simulated square. */
FVector2D AQZFluidTest::SampleVel(float u, float v, int32 Frame, float& OutSolid,
                                  float& OutDensity) const
{
	OutSolid = 1.f;
	OutDensity = 0.f;
	if (!bFieldOK || FieldW <= 0) return FVector2D::ZeroVector;

	const int32 F = FMath::Clamp(Frame, 0, FrameCount - 1);
	const int32 TX = F % TileCount;
	const int32 TY = F / TileCount;

	// Stay one texel inside the tile: a bilinear tap at the very edge reaches into the NEXT
	// frame's tile, which injects an unrelated velocity every time the flipbook advances.
	const float fx = FMath::Clamp(u, 0.f, 1.f) * (GridN - 2) + 0.5f;
	const float fy = FMath::Clamp(v, 0.f, 1.f) * (GridN - 2) + 0.5f;
	const int32 x0 = FMath::Clamp((int32)fx, 0, GridN - 2);
	const int32 y0 = FMath::Clamp((int32)fy, 0, GridN - 2);
	const float sx = fx - x0;
	const float sy = fy - y0;

	auto Tap = [&](int32 px, int32 py, float& R, float& G, float& B, float& A)
	{
		const int32 gx = TX * GridN + px;
		const int32 gy = TY * GridN + py;
		const int32 o = (gy * FieldW + gx) * 4;
		if (o < 0 || o + 3 >= Field.Num()) { R = G = 0.5f; B = A = 0.f; return; }
		B = Field[o + 0] / 255.f;     // BGRA on Windows: blue first, red third
		G = Field[o + 1] / 255.f;
		R = Field[o + 2] / 255.f;
		A = Field[o + 3] / 255.f;
	};

	float r00, g00, b00, a00, r10, g10, b10, a10, r01, g01, b01, a01, r11, g11, b11, a11;
	Tap(x0,     y0,     r00, g00, b00, a00);
	Tap(x0 + 1, y0,     r10, g10, b10, a10);
	Tap(x0,     y0 + 1, r01, g01, b01, a01);
	Tap(x0 + 1, y0 + 1, r11, g11, b11, a11);

	auto Mix = [&](float A00, float A10, float A01, float A11)
	{
		return FMath::Lerp(FMath::Lerp(A00, A10, sx), FMath::Lerp(A01, A11, sx), sy);
	};

	const float R = Mix(r00, r10, r01, r11);
	const float G = Mix(g00, g10, g01, g11);
	OutDensity = Mix(b00, b10, b01, b11);
	OutSolid = Mix(a00, a10, a01, a11);

	return FVector2D((R * 2.f - 1.f) * VMax, (G * 2.f - 1.f) * VMax);
}

void AQZFluidTest::Respawn(int32 i, FRandomStream& R)
{
	// Back at the inflow band the bake used: a strip on the left, centred vertically.
	P[i] = FVector2D(R.FRandRange(0.005f, 0.06f), 0.5f + R.FRandRange(-0.085f, 0.085f));
	Zoff[i] = R.FRandRange(-0.5f, 0.5f) * SlabThicknessUU;
	Age[i] = R.FRandRange(0.f, ParticleLife * 0.25f);
	Speed[i] = 0.f;
}

void AQZFluidTest::BeginPlay()
{
	Super::BeginPlay();
	Rng.Initialize(20260805);
	bFieldOK = ReadField();

	if (!Particles->GetStaticMesh())
	{
		if (UStaticMesh* Sphere = LoadObject<UStaticMesh>(
				nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
		{
			Particles->SetStaticMesh(Sphere);
		}
	}
	if (!Obstacle->GetStaticMesh())
	{
		if (UStaticMesh* Sphere = LoadObject<UStaticMesh>(
				nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
		{
			Obstacle->SetStaticMesh(Sphere);
		}
	}

	ParticleCount = FMath::Clamp(ParticleCount, 16, 60000);
	P.SetNum(ParticleCount);
	Zoff.SetNum(ParticleCount);
	Age.SetNum(ParticleCount);
	Speed.SetNum(ParticleCount);
	for (int32 i = 0; i < ParticleCount; ++i) Respawn(i, Rng);

	Particles->ClearInstances();
	TArray<FTransform> T;
	T.SetNum(ParticleCount);
	for (int32 i = 0; i < ParticleCount; ++i) T[i] = FTransform::Identity;
	Particles->AddInstances(T, false, false);

	UE_LOG(LogTemp, Warning, TEXT("[QZFluid] %d particles, field %s"),
		ParticleCount, bFieldOK ? TEXT("loaded") : TEXT("MISSING — nothing will move"));
}

void AQZFluidTest::Tick(float Dt)
{
	Super::Tick(Dt);
	if (Dt <= 0.f || P.Num() == 0) return;

	Clock += Dt;
	const float Loop = FrameCount / FMath::Max(FlipbookFPS, 0.01f);
	const float T01 = FMath::Fmod(Clock, Loop) / Loop;
	const int32 Frame = FMath::Clamp((int32)(T01 * FrameCount), 0, FrameCount - 1);

	// The obstacle follows the SAME parametric path the bake used. If these two ever disagree the
	// particles part around empty space and the sphere ploughs through them untouched — the most
	// obvious possible symptom, which is why it is worth keeping the expression identical.
	const float cx = 0.68f - 0.30f * T01;
	const float cy = 0.50f + 0.16f * FMath::Sin(T01 * 2.f * PI);
	const float rad = 0.11f;
	const FVector ObsLocal(
		(cx - 0.5f) * PlaneSizeUU,
		(cy - 0.5f) * PlaneSizeUU,
		0.f);
	Obstacle->SetRelativeLocation(ObsLocal);
	// /Engine/BasicShapes/Sphere is 100 uu across, so radius r maps to a scale of 2r*Plane/100.
	Obstacle->SetRelativeScale3D(FVector(2.f * rad * PlaneSizeUU / 100.f));

	TArray<FTransform> Xf;
	Xf.SetNum(P.Num());

	for (int32 i = 0; i < P.Num(); ++i)
	{
		float Solid = 1.f, Dens = 0.f;
		const FVector2D Vel = SampleVel(P[i].X, P[i].Y, Frame, Solid, Dens);

		P[i] += Vel * (SpeedScale / FMath::Max(PlaneSizeUU, 1.f)) * Dt;
		Age[i] += Dt;
		Speed[i] = FMath::Lerp(Speed[i], Vel.Size(), 0.35f);

		const bool bGone = (P[i].X < -0.02f || P[i].X > 1.02f ||
		                    P[i].Y < -0.02f || P[i].Y > 1.02f);
		if (bGone || Solid < 0.5f || Age[i] > ParticleLife)
		{
			Respawn(i, Rng);
		}

		const FVector Pos(
			(P[i].X - 0.5f) * PlaneSizeUU,
			(P[i].Y - 0.5f) * PlaneSizeUU,
			Zoff[i]);

		// Stretch along travel: a sphere scaled on one axis reads as motion blur for free, and it
		// is what separates a fluid from a cloud of dots.
		const float Sp = FMath::Clamp(Speed[i] / FMath::Max(VMax, 0.01f), 0.f, 1.f);
		const float Len = ParticleSizeUU * (1.f + StretchBySpeed * Sp);
		const FRotator Rot = Vel.IsNearlyZero()
			? FRotator::ZeroRotator
			: FRotator(0.f, FMath::RadiansToDegrees(FMath::Atan2(Vel.Y, Vel.X)), 0.f);

		// Fade the youngest and oldest by shrinking — no per-instance opacity without custom data,
		// and a particle that pops in at full size is the tell that this is not a real emitter.
		const float LifeFade = FMath::Min(Age[i] / 0.35f,
			FMath::Max(0.f, (ParticleLife - Age[i]) / 0.8f));
		const float Grow = FMath::Clamp(LifeFade, 0.f, 1.f) * FMath::Clamp(Dens * 1.6f, 0.15f, 1.f);

		Xf[i] = FTransform(Rot,
			Pos,
			FVector(Len * Grow, ParticleSizeUU * Grow, ParticleSizeUU * Grow) / 100.f);
	}

	Particles->BatchUpdateInstancesTransforms(0, Xf, false, true, true);
}
