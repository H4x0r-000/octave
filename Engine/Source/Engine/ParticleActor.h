#pragma once

#include "Nodes/Node.h"
#include "AssetRef.h"
#include "Assets/ParticleSystem.h"

#include "Nodes/3D/ParticleComponent.h"

class ParticleActor : public Actor
{
public:

    DECLARE_ACTOR(ParticleActor, Actor);

    virtual void Create() override;
    virtual void Tick(float deltaTime);

    void SetParticleSystem(ParticleSystem* system);

    static ParticleActor* SpawnParticleActor(World* world, glm::vec3 position, ParticleSystem* system);

protected:

    ParticleSystemRef mParticleSystem;
    ParticleComponent* mParticleComp = nullptr;
    float mTimeAlive = 0.0f;
};
