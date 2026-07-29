#include "QZMolecule.h"
#include "Components/SceneComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/ConstructorHelpers.h"

// element indices: 0=C 1=N 2=O 3=S 4=H 5=other
uint8 AQZMolecule::ElemFromSymbol(const FString& Sym, const FString& Name)
{
	FString S = Sym.TrimStartAndEnd().ToUpper();
	if (S.IsEmpty())
	{
		const FString N = Name.TrimStartAndEnd().ToUpper();
		if (N.StartsWith(TEXT("C"))) S = TEXT("C");
		else if (N.StartsWith(TEXT("N"))) S = TEXT("N");
		else if (N.StartsWith(TEXT("O"))) S = TEXT("O");
		else if (N.StartsWith(TEXT("S"))) S = TEXT("S");
		else if (N.StartsWith(TEXT("H"))) S = TEXT("H");
	}
	if (S == TEXT("C")) return 0;
	if (S == TEXT("N")) return 1;
	if (S == TEXT("O")) return 2;
	if (S == TEXT("S")) return 3;
	if (S == TEXT("H")) return 4;
	return 5;
}

void AQZMolecule::ElementRadii(uint8 Elem, float& VdW, float& Cov)
{
	switch (Elem)
	{
	case 0: VdW = 1.70f; Cov = 0.77f; break; // C
	case 1: VdW = 1.55f; Cov = 0.75f; break; // N
	case 2: VdW = 1.52f; Cov = 0.73f; break; // O
	case 3: VdW = 1.80f; Cov = 1.02f; break; // S
	case 4: VdW = 1.20f; Cov = 0.37f; break; // H
	default:VdW = 1.60f; Cov = 0.80f; break; // other
	}
}

static UInstancedStaticMeshComponent* MakeISM(AActor* Owner, USceneComponent* Root, const TCHAR* Name,
	UStaticMesh* Mesh, UMaterialInterface* Mat)
{
	UInstancedStaticMeshComponent* C = Owner->CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
	C->SetupAttachment(Root);
	C->SetMobility(EComponentMobility::Movable);
	C->SetCastShadow(false);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (Mesh) C->SetStaticMesh(Mesh);
	if (Mat)  C->SetMaterial(0, Mat);
	return C;
}

AQZMolecule::AQZMolecule()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereF(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylF(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Sphere = SphereF.Succeeded() ? SphereF.Object : nullptr;
	UStaticMesh* Cyl    = CylF.Succeeded()    ? CylF.Object    : nullptr;

	auto Load = [](const TCHAR* P) -> UMaterialInterface*
	{
		ConstructorHelpers::FObjectFinder<UMaterialInterface> F(P);
		return F.Succeeded() ? F.Object : nullptr;
	};
	UMaterialInterface* Mc  = Load(TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/MI_CPK_C.MI_CPK_C"));
	UMaterialInterface* Mn  = Load(TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/MI_CPK_N.MI_CPK_N"));
	UMaterialInterface* Mo  = Load(TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/MI_CPK_O.MI_CPK_O"));
	UMaterialInterface* Ms  = Load(TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/MI_CPK_S.MI_CPK_S"));
	UMaterialInterface* Mh  = Load(TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/MI_CPK_HL.MI_CPK_HL"));
	UMaterialInterface* Mx  = Load(TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/MI_CPK_H.MI_CPK_H"));
	UMaterialInterface* Mb  = Load(TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/MI_CPK_BOND.MI_CPK_BOND"));

	ISM_C    = MakeISM(this, Root, TEXT("ISM_C"),    Sphere, Mc);
	ISM_N    = MakeISM(this, Root, TEXT("ISM_N"),    Sphere, Mn);
	ISM_O    = MakeISM(this, Root, TEXT("ISM_O"),    Sphere, Mo);
	ISM_S    = MakeISM(this, Root, TEXT("ISM_S"),    Sphere, Ms);
	ISM_X    = MakeISM(this, Root, TEXT("ISM_X"),    Sphere, Mx);
	ISM_HL   = MakeISM(this, Root, TEXT("ISM_HL"),   Sphere, Mh);
	ISM_Bond = MakeISM(this, Root, TEXT("ISM_Bond"), Cyl,    Mb);

	// procedurally-generated animated volume hull (built from the atom cloud in GenerateHull)
	Hull = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Hull"));
	Hull->SetupAttachment(Root);
	Hull->SetMobility(EComponentMobility::Movable);
	Hull->SetCastShadow(false);
	Hull->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> HullMatF(TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/M_MolHull.M_MolHull"));
	if (HullMatF.Succeeded()) Hull->SetMaterial(0, HullMatF.Object);
}

void AQZMolecule::GenerateHull(const TArray<FAtomRec>& Rec, const FVector& Centroid)
{
	if (!Hull) return;
	Hull->ClearAllMeshSections();
	Hull->SetVisibility(bShowHull);
	if (!bShowHull || Rec.Num() == 0) return;

	// --- unit icosphere (subdivided icosahedron) ---
	const float t = (1.f + FMath::Sqrt(5.f)) * 0.5f;
	TArray<FVector> V = {
		{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
		{0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1} };
	for (FVector& v : V) v.Normalize();
	TArray<FIntVector> F = {
		{0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11}, {1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
		{3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9}, {4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1} };
	const int32 Sub = FMath::Clamp(HullSubdiv, 0, 4);
	for (int32 s = 0; s < Sub; ++s)
	{
		TArray<FIntVector> F2;
		TMap<int64, int32> Cache;
		auto Mid = [&](int32 a, int32 b) -> int32 {
			const int64 key = ((int64)FMath::Min(a, b) << 32) | (uint32)FMath::Max(a, b);
			if (int32* p = Cache.Find(key)) return *p;
			FVector m = V[a] + V[b]; m.Normalize();
			const int32 idx = V.Add(m); Cache.Add(key, idx); return idx;
		};
		for (const FIntVector& f : F)
		{
			const int32 a = Mid(f.X, f.Y), b = Mid(f.Y, f.Z), c = Mid(f.Z, f.X);
			F2.Add({ f.X,a,c }); F2.Add({ f.Y,b,a }); F2.Add({ f.Z,c,b }); F2.Add({ a,b,c });
		}
		F = MoveTemp(F2);
	}

	// --- shrink-wrap: each vertex sits at the atom cloud's support distance along its direction ---
	TArray<FVector> Verts; Verts.SetNum(V.Num());
	TArray<FVector> Norms; Norms.SetNum(V.Num());
	for (int32 i = 0; i < V.Num(); ++i)
	{
		const FVector Dir = V[i];
		float Best = 0.f;
		for (const FAtomRec& A : Rec)
		{
			float vdw, cov; ElementRadii(A.Elem, vdw, cov);
			const FVector P = (A.Pos - Centroid) * ModelScale;
			const float proj = FVector::DotProduct(P, Dir) + vdw * BallScale * ModelScale;
			if (proj > Best) Best = proj;
		}
		Verts[i] = Dir * (Best * HullInflate);
		Norms[i] = Dir;
	}
	TArray<int32> Tris; Tris.Reserve(F.Num() * 3);
	for (const FIntVector& f : F) { Tris.Add(f.X); Tris.Add(f.Y); Tris.Add(f.Z); }
	const TArray<FVector2D> UV0;
	const TArray<FLinearColor> Cols;
	const TArray<FProcMeshTangent> Tans;
	Hull->CreateMeshSection_LinearColor(0, Verts, Tris, Norms, UV0, Cols, Tans, false);
	if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/M_MolHull.M_MolHull")))
		Hull->SetMaterial(0, M);
}

UInstancedStaticMeshComponent* AQZMolecule::IsmForElem(uint8 Elem) const
{
	switch (Elem)
	{
	case 0: return ISM_C;
	case 1: return ISM_N;
	case 2: return ISM_O;
	case 3: return ISM_S;
	case 4: return ISM_X; // H -> white ISM_X
	default:return ISM_X;
	}
}

void AQZMolecule::OnConstruction(const FTransform& T)
{
	Super::OnConstruction(T);
	// Skip the edit-time build during headless commandlets (level-integration script) — it builds at BeginPlay.
	// Building thousands of instances inside a commandlet's construction pass can crash the null-RHI context.
	if (IsRunningCommandlet()) return;
	Build();
}

void AQZMolecule::BeginPlay()
{
	Super::BeginPlay();
	Build();
}

bool AQZMolecule::ParsePdb(TArray<FAtomRec>& Out, FVector& Centroid) const
{
	FString Path = PdbPath;
	if (FPaths::IsRelative(Path)) Path = FPaths::Combine(FPaths::ProjectDir(), Path);
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		UE_LOG(LogTemp, Warning, TEXT("[QZMolecule] cannot read PDB: %s"), *Path);
		return false;
	}
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, false);
	FVector Sum(0.f);
	for (const FString& Ln : Lines)
	{
		if (!(Ln.StartsWith(TEXT("ATOM")) || Ln.StartsWith(TEXT("HETATM")))) continue;
		if (Ln.Len() < 54) continue;
		const FString Name = Ln.Mid(12, 4);
		const FString Res  = Ln.Mid(22, 4);
		const FString Sym  = (Ln.Len() >= 78) ? Ln.Mid(76, 2) : FString();
		FAtomRec A;
		// HANDEDNESS: PDB (like Blender) is RIGHT-handed; Unreal is LEFT-handed. The static NirA meshes get
		// this conversion for free — Blender exports FBX and UE's importer negates Y on the way in. This
		// parser used to drop raw PDB coordinates straight into UE space, so the procedural molecule rendered
		// as the MIRROR IMAGE of the static ones (Michael: "seems mirrored" — he was right).
		// Negating Y is a reflection (det = -1), which is exactly what converts between the two conventions.
		// This matters beyond looks: a mirrored L-amino-acid protein is D-, i.e. physically wrong, and this
		// piece is presented as science.
		A.Pos  = FVector(FCString::Atod(*Ln.Mid(30, 8)), -FCString::Atod(*Ln.Mid(38, 8)), FCString::Atod(*Ln.Mid(46, 8)));
		A.Res  = FCString::Atoi(*Res);
		A.Elem = ElemFromSymbol(Sym, Name);
		Sum += A.Pos;
		Out.Add(A);
	}
	if (Out.Num() == 0) return false;
	Centroid = Sum / Out.Num();

	// CENTRE ON THE HIGHLIGHT RESIDUE, not the protein centroid.
	// Michael: "two of the nirA display options align perfectly the third is off" — this was the third.
	// The two STATIC versions were physically re-centred on MET169 in Blender (nira_align.py /
	// align_nira_highres.py put the residue at the mesh origin, HL1 verified at 0.000), so placing their
	// actor on the Anchor puts MET169 on the zoom centre. This procedural version placed atoms at
	// (Pos - Centroid), i.e. it centred the WHOLE PROTEIN's average — leaving MET169 ~1000 UU off-axis, so
	// swapping to it with X jumped the frame and the dive no longer converged on the residue.
	// Centring on the same residue makes all three versions agree by construction.
	if (CentreResidue != 0)
	{
		// Use the residue's BOUNDS centre (min+max)/2 — NOT the mean of its atom positions. Blender's
		// align_met169.py/nira_align.py centred the static meshes on the HL1 objects' combined BOUNDS centre,
		// and for an asymmetric side chain like methionine the two differ by a few tenths of an Angstrom.
		// Matching the method exactly is what makes all three versions land on the same point.
		FVector Lo(BIG_NUMBER), Hi(-BIG_NUMBER);
		int32 RN = 0;
		for (const FAtomRec& A : Out)
		{
			if (A.Res != CentreResidue) continue;
			Lo = Lo.ComponentMin(A.Pos); Hi = Hi.ComponentMax(A.Pos);
			++RN;
		}
		if (RN > 0)
		{
			Centroid = (Lo + Hi) * 0.5f;
			UE_LOG(LogTemp, Log, TEXT("[QZMolecule] centred on residue %d (%d atoms), bounds centre %s"),
				CentreResidue, RN, *Centroid.ToString());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[QZMolecule] CentreResidue %d not found in %s — falling back to the "
				"protein centroid; this version will NOT line up with the pre-centred static meshes."),
				CentreResidue, *Path);
		}
	}
	return true;
}

void AQZMolecule::Build()
{
	UInstancedStaticMeshComponent* All[7] = { ISM_C, ISM_N, ISM_O, ISM_S, ISM_X, ISM_HL, ISM_Bond };
	for (UInstancedStaticMeshComponent* C : All) { if (C) C->ClearInstances(); }

	// robust runtime material assignment (constructor-time FObjectFinder can miss; this always applies)
	auto Assign = [](UInstancedStaticMeshComponent* C, const TCHAR* P) {
		if (C) { if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, P)) C->SetMaterial(0, M); }
	};
	Assign(ISM_C,    TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/MI_CPK_C.MI_CPK_C"));
	Assign(ISM_N,    TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/MI_CPK_N.MI_CPK_N"));
	Assign(ISM_O,    TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/MI_CPK_O.MI_CPK_O"));
	Assign(ISM_S,    TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/MI_CPK_S.MI_CPK_S"));
	Assign(ISM_X,    TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/MI_CPK_H.MI_CPK_H"));
	Assign(ISM_HL,   TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/MI_CPK_HL.MI_CPK_HL"));
	Assign(ISM_Bond, TEXT("/Game/QuantumZoom/BLOCKOUT/_mats/MI_CPK_BOND.MI_CPK_BOND"));

	if (PdbPath.IsEmpty()) return;

	TArray<FAtomRec> Rec;
	FVector Centroid;
	if (!ParsePdb(Rec, Centroid)) return;

	// procedural hull — verts already in centred+scaled UE space, so keep the component at identity
	if (Hull) { Hull->SetRelativeLocation(FVector::ZeroVector); Hull->SetRelativeScale3D(FVector::OneVector); }
	GenerateHull(Rec, Centroid);

	const bool bHi = (HighlightResidue != 0);

	// ---- atoms: route each into the ISM for its element (or the gold highlight ISM) ----
	for (const FAtomRec& A : Rec)
	{
		float VdW, Cov; ElementRadii(A.Elem, VdW, Cov);
		const bool bIsHi = bHi && (A.Res == HighlightResidue);
		UInstancedStaticMeshComponent* C = bIsHi ? ISM_HL.Get() : IsmForElem(A.Elem);
		if (!C) continue;
		const FVector P = (A.Pos - Centroid) * ModelScale;
		const float   R = VdW * BallScale * ModelScale;
		const float   S = (R / 50.f) * (bIsHi ? 1.25f : 1.0f);   // engine sphere r=50 at scale 1
		C->AddInstance(FTransform(FRotator::ZeroRotator, P, FVector(S)));
	}

	// DIAGNOSTIC: report what was ACTUALLY built, in world space. The ISMs only exist at runtime, so every
	// headless check of this actor is blind — this is the only honest way to compare the procedural version
	// against the two static ones (Michael: it is still the X-cycled one that is off).
	{
		FVector Lo(BIG_NUMBER), Hi(-BIG_NUMBER), HlLo(BIG_NUMBER), HlHi(-BIG_NUMBER);
		int32 NHl = 0;
		for (const FAtomRec& A : Rec)
		{
			const FVector P = (A.Pos - Centroid) * ModelScale;
			Lo = Lo.ComponentMin(P); Hi = Hi.ComponentMax(P);
			if (bHi && A.Res == HighlightResidue) { HlLo = HlLo.ComponentMin(P); HlHi = HlHi.ComponentMax(P); ++NHl; }
		}
		const FTransform T = GetActorTransform();
		UE_LOG(LogTemp, Warning, TEXT("[QZMolecule] BUILT %d atoms | ModelScale=%.2f CentreResidue=%d"),
			Rec.Num(), ModelScale, CentreResidue);
		UE_LOG(LogTemp, Warning, TEXT("[QZMolecule]   local bbox = %s .. %s  (size %s)"),
			*Lo.ToString(), *Hi.ToString(), *(Hi - Lo).ToString());
		UE_LOG(LogTemp, Warning, TEXT("[QZMolecule]   actor at %s  scale %s"),
			*T.GetLocation().ToString(), *T.GetScale3D().ToString());
		if (NHl > 0)
		{
			const FVector HlC = (HlLo + HlHi) * 0.5f;
			UE_LOG(LogTemp, Warning, TEXT("[QZMolecule]   residue %d local centre = %s  -> WORLD %s"),
				HighlightResidue, *HlC.ToString(), *T.TransformPosition(HlC).ToString());
			UE_LOG(LogTemp, Warning, TEXT("[QZMolecule]   ^ this WORLD point must equal the Anchor; anything else IS the misalignment"));
		}
	}

	// ---- bonds: spatial hash in Angstrom space -> instanced cylinders ----
	if (bBuildBonds && ISM_Bond)
	{
		const float Cell = 2.2f;
		TMap<FIntVector, TArray<int32>> Grid;
		auto Key = [Cell](const FVector& P) {
			return FIntVector(FMath::FloorToInt(P.X / Cell), FMath::FloorToInt(P.Y / Cell), FMath::FloorToInt(P.Z / Cell));
		};
		for (int32 i = 0; i < Rec.Num(); ++i) Grid.FindOrAdd(Key(Rec[i].Pos)).Add(i);

		for (int32 i = 0; i < Rec.Num(); ++i)
		{
			const FIntVector K = Key(Rec[i].Pos);
			float vi, covi; ElementRadii(Rec[i].Elem, vi, covi);
			for (int32 dx = -1; dx <= 1; ++dx) for (int32 dy = -1; dy <= 1; ++dy) for (int32 dz = -1; dz <= 1; ++dz)
			{
				const TArray<int32>* Cellp = Grid.Find(FIntVector(K.X + dx, K.Y + dy, K.Z + dz));
				if (!Cellp) continue;
				for (int32 j : *Cellp)
				{
					if (j <= i) continue;
					float vj, covj; ElementRadii(Rec[j].Elem, vj, covj);
					const float d = FVector::Dist(Rec[i].Pos, Rec[j].Pos);
					if (d < 0.4f || d > (covi + covj) * 1.30f) continue;
					const FVector A = (Rec[i].Pos - Centroid) * ModelScale;
					const FVector B = (Rec[j].Pos - Centroid) * ModelScale;
					const FVector Dir = (B - A).GetSafeNormal();
					if (Dir.IsNearlyZero()) continue;
					const float Len = (B - A).Size();
					const FQuat Q = FQuat::FindBetweenNormals(FVector::UpVector, Dir);
					const float Rr = (BondRadius * ModelScale) / 50.f;
					ISM_Bond->AddInstance(FTransform(Q, (A + B) * 0.5f, FVector(Rr, Rr, Len / 100.f)));
				}
			}
		}
	}

	int32 nAtoms = 0;
	for (int i = 0; i < 6; ++i) if (All[i]) nAtoms += All[i]->GetInstanceCount();
	UE_LOG(LogTemp, Log, TEXT("[QZMolecule] built %d atoms, %d bonds from %s"),
		nAtoms, ISM_Bond ? ISM_Bond->GetInstanceCount() : 0, *PdbPath);
}
