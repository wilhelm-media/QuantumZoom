#include "QOrbitalNoiseBridge.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "NiagaraComponent.h"
#include "EngineUtils.h"

AQOrbitalNoiseBridge::AQOrbitalNoiseBridge()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AQOrbitalNoiseBridge::Tick(float Dt)
{
	Super::Tick(Dt);
	if (!Collection) return;
	const float V = UKismetMaterialLibrary::GetScalarParameterValue(this, Collection, CollectionParameter);
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		TArray<UNiagaraComponent*> NCs;
		It->GetComponents<UNiagaraComponent>(NCs);
		for (UNiagaraComponent* NC : NCs)
			NC->SetVariableFloat(NiagaraParameter, V);
	}
}
