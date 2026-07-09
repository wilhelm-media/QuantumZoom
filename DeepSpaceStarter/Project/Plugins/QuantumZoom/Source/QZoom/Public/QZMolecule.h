#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "QZMolecule.generated.h"

class USceneComponent;
class UInstancedStaticMeshComponent;

/**
 * AQZMolecule — data-driven molecular renderer. Parses an AlphaFold/PDB heavy-atom structure at load and
 * builds it as GPU-instanced spheres (atoms) + cylinders (bonds). Colour is PER ELEMENT via a separate ISM
 * per element, each carrying its MI_CPK_* material (C grey, N blue, O red, S yellow, HL gold) — no baked
 * meshes, no material slots, no per-instance custom data. Change the .pdb and rebuild. Deterministic
 * (CPU-placed) so it stays in lockstep across the nDisplay cluster.
 *
 * Tag "QZStation" + a numeric index like the other heroes so the pawn scales/fades it.
 */
UCLASS()
class QZOOM_API AQZMolecule : public AActor
{
	GENERATED_BODY()
public:
	AQZMolecule();

	UPROPERTY(EditAnywhere, Category = "Molecule") FString PdbPath;
	UPROPERTY(EditAnywhere, Category = "Molecule") float ModelScale = 20.0f;   // UE units per Angstrom
	UPROPERTY(EditAnywhere, Category = "Molecule") float BallScale  = 0.30f;   // atom radius as fraction of vdW
	UPROPERTY(EditAnywhere, Category = "Molecule") float BondRadius = 0.16f;   // Angstrom
	UPROPERTY(EditAnywhere, Category = "Molecule") bool  bBuildBonds = true;
	UPROPERTY(EditAnywhere, Category = "Molecule") int32 HighlightResidue = 0; // 0 = none
	UPROPERTY(EditAnywhere, Category = "Molecule") bool  bRebuildNow = false;  // toggle to rebuild in-editor
	// Procedurally-generated animated volume hull (icosphere shrink-wrapped to the atom cloud).
	UPROPERTY(EditAnywhere, Category = "Hull") bool  bShowHull = true;
	UPROPERTY(EditAnywhere, Category = "Hull") int32 HullSubdiv = 3;       // icosphere subdivisions (0..4)
	UPROPERTY(EditAnywhere, Category = "Hull") float HullInflate = 1.12f;  // sit just outside the atom envelope

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UProceduralMeshComponent> Hull;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> ISM_C;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> ISM_N;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> ISM_O;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> ISM_S;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> ISM_X;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> ISM_HL;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> ISM_Bond;

	virtual void OnConstruction(const FTransform& T) override;
	virtual void BeginPlay() override;

	void Build();

private:
	struct FAtomRec { FVector Pos; int32 Res; uint8 Elem; };
	static uint8 ElemFromSymbol(const FString& Sym, const FString& Name);
	static void  ElementRadii(uint8 Elem, float& VdW, float& Cov);
	bool ParsePdb(TArray<FAtomRec>& Out, FVector& Centroid) const;
	UInstancedStaticMeshComponent* IsmForElem(uint8 Elem) const;
	void GenerateHull(const TArray<FAtomRec>& Rec, const FVector& Centroid);
};
