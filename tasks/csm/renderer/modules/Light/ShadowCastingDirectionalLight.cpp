#include "ShadowCastingDirectionalLight.hpp"

#define GLM_ENABLE_EXPERIMENTAL

#include "glm/gtx/string_cast.hpp"
#include "spdlog/spdlog.h"

#include <etna/GlobalContext.hpp>


ShadowCastingDirectionalLight::ShadowCastingDirectionalLight(const CreateInfo& info)
  : shaderInfo(
      {.light = info.light, .cascadesAmount = static_cast<uint32_t>(info.planes.size() - 1), .planesOffset = info.planesOffset})
  , projViewMatrices({})
  , planes(info.planes)
{
  projViewMatrices.resize(shaderInfo.cascadesAmount);

  infoBuffer = etna::get_context().createBuffer(
    etna::Buffer::CreateInfo{
      .size = sizeof(ShadowCastingDirectionalLight::ShaderInfo) +
        // sizeof(ShadowCastingDirectionalLight::ShaderInfo::light) +
        //   sizeof(ShadowCastingDirectionalLight::ShaderInfo::cascadesAmount) +
        // sizeof(ShadowCastingDirectionalLight::ShaderInfo::_padding) +
        shaderInfo.cascadesAmount * sizeof(glm::mat4x4) + planes.size() * sizeof(float),
      .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO,
      .allocationCreate =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .name = fmt::format("ShadowCastingDirLightInfo")});
}

void ShadowCastingDirectionalLight::update(const Camera& main_camera, float aspect_ratio)
{
  Camera frustumCamera = main_camera;

  for (std::size_t i = 0; i < shaderInfo.cascadesAmount; i++)
  {
    frustumCamera.zNear = planes[i] - (i == 0 ? 0.0f : shaderInfo.planesOffset);
    frustumCamera.zFar = planes[i + 1] + (i == shaderInfo.cascadesAmount - 1 ? 0.0f : shaderInfo.planesOffset);

    glm::mat4x4 proj = frustumCamera.projTm(aspect_ratio);

    std::array corners = getWorldSpaceFrustumCorners(proj * frustumCamera.viewTm());

    glm::vec3 center = getFrustumCenter(corners);

    // float radius = 0.0f;

    // for (const auto& corner : corners)
    // {
    //   float distance = glm::length(corner - center);
    //   radius = glm::max(radius, distance);
    // }

    // radius = std::ceil(radius * 16.0f) / 16.0f;

    // glm::vec3 maxExtents = glm::vec3(radius);
    // // spdlog::info("cascade {}, radius - {}", i, radius);

    // glm::vec3 minExtents = -maxExtents;


    // glm::mat4x4 lightProj = glm::orthoLH_ZO(
    //   minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, 0.0f, maxExtents.z);

    // spdlog::info(
    //   "cascade {}, bounding box - x:{}, -x:{}, y:{}, -y:{}, z:{}, -z:{}",
    //   i,
    //   minExtents.x,
    //   maxExtents.x,
    //   minExtents.y,
    //   maxExtents.y,
    //   0.0f,
    //   maxExtents.z - minExtents.z);

    // projViewMatrices[i] = lightProj * lightView;

    glm::mat4x4 lightView = glm::lookAtLH(center - shaderInfo.light.direction, center, {0, 1, 0});

    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::min();
    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::min();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = std::numeric_limits<float>::min();

    for (const auto& corner : corners)
    {
      auto lightSpaceCorner = lightView * glm::vec4(corner, 1.0f);

      minX = std::min(minX, lightSpaceCorner.x);
      maxX = std::max(maxX, lightSpaceCorner.x);
      minY = std::min(minY, lightSpaceCorner.y);
      maxY = std::max(maxY, lightSpaceCorner.y);
      minZ = std::min(minZ, lightSpaceCorner.z);
      maxZ = std::max(maxZ, lightSpaceCorner.z);
    }

    // float zMult = 10.0f;
    // if (minZ < 0.25)
    // {
    //   minZ *= zMult;
    // }
    // else
    // {
    //   minZ /= zMult;
    // }
    // if (maxZ < 0.25)
    // {
    //   maxZ /= zMult;
    // }
    // else
    // {
    //   maxZ *= zMult;
    // }

    // spdlog::info("projection matrix {} - {},", i,
    // glm::to_string(shaderInfo.projViewMatrices[i]));
    glm::mat4x4 lightProj = glm::orthoLH_ZO(minX, maxX, minY, maxY, minZ - 350.0f, maxZ);

    projViewMatrices[i] = lightProj * lightView;
  }

  infoBuffer.map();

  uint32_t offset = 0;
  // sizeof(ShadowCastingDirectionalLight::ShaderInfo::light) +
  //   sizeof(ShadowCastingDirectionalLight::ShaderInfo::cascadesAmount) +
  //   sizeof(ShadowCastingDirectionalLight::ShaderInfo::_padding);

  std::memcpy(
    infoBuffer.data() + offset, &shaderInfo, sizeof(ShadowCastingDirectionalLight::ShaderInfo));
  // sizeof(ShadowCastingDirectionalLight::ShaderInfo::_padding));

  offset += sizeof(ShadowCastingDirectionalLight::ShaderInfo);

  // std::memcpy(
  //   infoBuffer.data() + offset,
  //   &shaderInfo.cascadesAmount,
  //   sizeof(ShadowCastingDirectionalLight::ShaderInfo::cascadesAmount));

  // offset += sizeof(ShadowCastingDirectionalLight::ShaderInfo::cascadesAmount);

  // std::memcpy(infoBuffer.data() + offset, &shaderInfo.light, offset);
  std::memcpy(
    infoBuffer.data() + offset,
    projViewMatrices.data(),
    sizeof(glm::mat4x4) * projViewMatrices.size());

  offset += sizeof(glm::mat4x4) * projViewMatrices.size();

  std::memcpy(infoBuffer.data() + offset, planes.data(), sizeof(float) * planes.size());

  infoBuffer.unmap();
}

// std::vector<std::byte> ShadowCastingDirectionalLight::getInfoBuffer() const
// {
//   const uint32_t matricesOffset = sizeof(ShadowCastingDirectionalLight::ShaderInfo::light) +
//     sizeof(ShadowCastingDirectionalLight::ShaderInfo::cascadesAmount);

//   std::memcpy(infoBuffer.data(), &shaderInfo, matricesOffset);

//   std::memcpy(
//     infoBuffer.data() + matricesOffset,
//     shaderInfo.projViewMatrices.data(),
//     sizeof(glm::mat4x4) * shaderInfo.projViewMatrices.size());

//   return infoBuffer;
// }

std::array<glm::vec3, 8> ShadowCastingDirectionalLight::getWorldSpaceFrustumCorners(
  const glm::mat4x4& proj_view)
{
  glm::mat4x4 invProjView = glm::inverse(proj_view);

  std::array corners = {
    glm::vec3(-1, -1, 0),
    glm::vec3(1, -1, 0),
    glm::vec3(-1, 1, 0),
    glm::vec3(1, 1, 0),
    glm::vec3(-1, -1, 1),
    glm::vec3(1, -1, 1),
    glm::vec3(-1, 1, 1),
    glm::vec3(1, 1, 1)};

  for (auto& corner : corners)
  {
    glm::vec4 invCorner = invProjView * glm::vec4(corner, 1.0f);
    corner = invCorner / invCorner.w;
  }

  return corners;
}

glm::vec3 ShadowCastingDirectionalLight::getFrustumCenter(const std::array<glm::vec3, 8>& corners)
{
  glm::vec3 center = {0, 0, 0};

  for (const auto& corner : corners)
  {
    center += corner;
  }

  center /= 8.0f;

  return center;
}
