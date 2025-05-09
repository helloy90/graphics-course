#pragma once

#include <glm/glm.hpp>


class Emitter
{
public:
  explicit Emitter(std::uint32_t max_amount);

  void update();

public:
    enum ParticleType : uint32_t {
        Invalid = ~uint32_t(0),
        Voxel = 0,
        Pixel = 1
    };

private:
  struct ParticleCPU
  {
    glm::vec4 posAndBirthTime;
    glm::vec3 velocity;
  };

  struct EmitterInfo
  {
    glm::vec3 spawnpoint;
    float spawnRadius;
    glm::vec3 initialVelocity;
    float speedRandomness; // dispersion from initial
    float lifetime;
    float spawnRate; // per second
  };

private:
  void sort();

private:
  std::uint32_t maxParticlesAmount;
};
