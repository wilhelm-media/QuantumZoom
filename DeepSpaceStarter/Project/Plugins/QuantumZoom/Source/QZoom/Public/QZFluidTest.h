// QZFluidTest — particles advected by a BAKED 2D fluid field, FluidNinja-style.
//
// Nothing is simulated at runtime. bake_fluid2d.py solved the fluid offline and packed it into a
// flipbook; this actor samples that texture per particle and integrates. The cost is a texture
// read, not a solver, which is the entire point of the technique.
//
// WHY C++ AND NOT NIAGARA
// Niagara is the production answer and this is not trying to replace it. But Niagara emitters
// cannot be authored from Python in 5.7 — the system asset can be created, the emitters cannot —
// so a Niagara version could not be built or verified headlessly, only hand-wired. This runs
// today, proves whether the look and the behaviour are right, and doubles as the reference the
// Niagara graph has to match. It is CPU-side and therefore the wrong tool past a few tens of
// thousands of particles.
//
// THE FIELD
//   R,G  velocity, remapped from [-VMax, VMax]
//   B    density   (used for colour/brightness, not for motion)
//   A    obstacle mask, 0 inside the sphere — particles entering it are respawned
// The flipbook is TileCount x TileCount frames of GridN pixels.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "QZFluidTest.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UTexture2D;

UCLASS()
class QZOOM_API AQZFluidTest : public AActor
{
	GENERATED_BODY()

public:
	AQZFluidTest();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** The baked flipbook. Must be uncompressed and NOT sRGB — a gamma-encoded velocity field
	 *  reads as a constant wind, and block compression turns the vortices into 4x4 mush. */
	UPROPERTY(EditAnywhere, Category="QZFluid")
	TObjectPtr<UTexture2D> FluidTexture = nullptr;

	UPROPERTY(EditAnywhere, Category="QZFluid") int32 GridN = 128;
	UPROPERTY(EditAnywhere, Category="QZFluid") int32 TileCount = 8;
	UPROPERTY(EditAnywhere, Category="QZFluid") int32 FrameCount = 64;

	/** Must match the bake. Written into vfx/out/fluid2d.json so the two cannot drift apart. */
	UPROPERTY(EditAnywhere, Category="QZFluid") float VMax = 13.f;

	UPROPERTY(EditAnywhere, Category="QZFluid") float FlipbookFPS = 24.f;

	/** World size of the simulated square. */
	UPROPERTY(EditAnywhere, Category="QZFluid") float PlaneSizeUU = 4000.f;

	/** The sim is 2D; particles get a little depth so the stream reads as a volume rather than a
	 *  sheet. Velocity stays planar — this is thickness, not a third axis of simulation. */
	UPROPERTY(EditAnywhere, Category="QZFluid") float SlabThicknessUU = 420.f;

	UPROPERTY(EditAnywhere, Category="QZFluid", meta=(ClampMin="16", ClampMax="60000"))
	int32 ParticleCount = 9000;

	/** Field units to world uu per second. */
	UPROPERTY(EditAnywhere, Category="QZFluid") float SpeedScale = 130.f;

	/** Particle size, and how much faster particles stretch. */
	UPROPERTY(EditAnywhere, Category="QZFluid") float ParticleSizeUU = 9.f;
	UPROPERTY(EditAnywhere, Category="QZFluid") float StretchBySpeed = 2.2f;

	/** Seconds before a particle is recycled even if it never leaves the domain — without this,
	 *  particles caught in a vortex orbit forever and the inflow slowly starves. */
	UPROPERTY(EditAnywhere, Category="QZFluid") float ParticleLife = 4.5f;

	UPROPERTY(VisibleAnywhere, Category="QZFluid")
	TObjectPtr<UInstancedStaticMeshComponent> Particles = nullptr;

	/** The obstacle, moved along exactly the path the bake used. */
	UPROPERTY(VisibleAnywhere, Category="QZFluid")
	TObjectPtr<UStaticMeshComponent> Obstacle = nullptr;

private:
	bool  ReadField();
	FVector2D SampleVel(float u, float v, int32 Frame, float& OutSolid, float& OutDensity) const;
	void  Respawn(int32 i, FRandomStream& R);

	TArray<uint8>  Field;          // raw mip 0, BGRA
	int32          FieldW = 0, FieldH = 0;
	bool           bFieldOK = false;

	TArray<FVector2D> P;           // particle position in field space, 0..1
	TArray<float>     Zoff;        // slab offset
	TArray<float>     Age;
	TArray<float>     Speed;
	float             Clock = 0.f;
	FRandomStream     Rng;
};
