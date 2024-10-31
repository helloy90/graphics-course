#include "Baker.hpp"

#include <etna/Assert.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/fwd.hpp>
#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <cmath>
#include <utility>

Baker::Baker(std::filesystem::path path)
  : filepath(path)
{
}

void Baker::run()
{
  auto maybeModel = loadFile();
  if (!maybeModel.has_value())
  {
    return;
  }

  auto model = *maybeModel;

  auto bakedMeshes = processMeshes(model);

  changeBuffer(model, bakedMeshes);
  changeBufferViews(model, bakedMeshes);
  modifyAccessors(model, bakedMeshes);

  saveFormatted(model);
  spdlog::info("Bakery complete!");
}

std::optional<tinygltf::Model> Baker::loadFile()
{
  auto fileExt = filepath.extension();
  if (fileExt != ".gltf")
  {
    spdlog::error("glTF: Unknown glTF file extension: '{}'. Expected .gltf.");
    return std::nullopt;
  }

  tinygltf::Model model;

  std::string error;
  std::string warning;
  bool success = false;

  success = loader.LoadASCIIFromFile(&model, &error, &warning, filepath.string().c_str());

  if (!success)
  {
    spdlog::error("glTF: Failed to load model!");
    if (!error.empty())
      spdlog::error("glTF: {}", error);
  }

  if (!warning.empty())
    spdlog::warn("glTF: {}", warning);

  if (
    !model.extensions.empty() || !model.extensionsRequired.empty() || !model.extensionsUsed.empty())
    spdlog::warn("glTF: No glTF extensions are currently implemented!");

  model.extensionsUsed.push_back("KHR_mesh_quantization");
  model.extensionsRequired.push_back("KHR_mesh_quantization");

  return model;
}

Baker::BakedMeshes Baker::processMeshes(const tinygltf::Model& model) const
{
  BakedMeshes result;
  {
    std::size_t vertexBytes = 0;
    std::size_t indexBytes = 0;
    for (const auto& bufView : model.bufferViews)
    {
      switch (bufView.target)
      {
      case TINYGLTF_TARGET_ARRAY_BUFFER:
        vertexBytes += bufView.byteLength;
        break;
      case TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER:
        indexBytes += bufView.byteLength;
        break;
      default:
        break;
      }
    }
    result.vertices.reserve(vertexBytes / sizeof(Vertex));
    result.indices.reserve(indexBytes / sizeof(std::uint32_t));
  }

  {
    std::size_t totalPrimitives = 0;
    for (const auto& mesh : model.meshes)
      totalPrimitives += mesh.primitives.size();
    result.relems.reserve(totalPrimitives);
  }

  result.meshes.reserve(model.meshes.size());

  for (const auto& mesh : model.meshes)
  {
    result.meshes.push_back(Mesh{
      .firstRelem = static_cast<std::uint32_t>(result.relems.size()),
      .relemCount = static_cast<std::uint32_t>(mesh.primitives.size()),
    });

    for (const auto& prim : mesh.primitives)
    {
      if (prim.mode != TINYGLTF_MODE_TRIANGLES)
      {
        spdlog::warn(
          "Encountered a non-triangles primitive, these are not supported for now, skipping it!");
        --result.meshes.back().relemCount;
        continue;
      }

      const auto normalIt = prim.attributes.find("NORMAL");
      const auto tangentIt = prim.attributes.find("TANGENT");
      const auto texcoordIt = prim.attributes.find("TEXCOORD_0");

      const bool hasNormals = normalIt != prim.attributes.end();
      const bool hasTangents = tangentIt != prim.attributes.end();
      const bool hasTexcoord = texcoordIt != prim.attributes.end();
      std::array accessorIndices{
        prim.indices,
        prim.attributes.at("POSITION"),
        hasNormals ? normalIt->second : -1,
        hasTangents ? tangentIt->second : -1,
        hasTexcoord ? texcoordIt->second : -1,
      };

      std::array accessors{
        &model.accessors[prim.indices],
        &model.accessors[accessorIndices[1]],
        hasNormals ? &model.accessors[accessorIndices[2]] : nullptr,
        hasTangents ? &model.accessors[accessorIndices[3]] : nullptr,
        hasTexcoord ? &model.accessors[accessorIndices[4]] : nullptr,
      };

      std::array bufViews{
        &model.bufferViews[accessors[0]->bufferView],
        &model.bufferViews[accessors[1]->bufferView],
        hasNormals ? &model.bufferViews[accessors[2]->bufferView] : nullptr,
        hasTangents ? &model.bufferViews[accessors[3]->bufferView] : nullptr,
        hasTexcoord ? &model.bufferViews[accessors[4]->bufferView] : nullptr,
      };

      result.relems.push_back(RenderElement{
        .vertexOffset = static_cast<std::uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<std::uint32_t>(result.indices.size()),
        .indexCount = static_cast<std::uint32_t>(accessors[0]->count),
      });

      const std::size_t vertexCount = accessors[1]->count;

      std::array ptrs{
        reinterpret_cast<const std::byte*>(model.buffers[bufViews[0]->buffer].data.data()) +
          bufViews[0]->byteOffset + accessors[0]->byteOffset,
        reinterpret_cast<const std::byte*>(model.buffers[bufViews[1]->buffer].data.data()) +
          bufViews[1]->byteOffset + accessors[1]->byteOffset,
        hasNormals
          ? reinterpret_cast<const std::byte*>(model.buffers[bufViews[2]->buffer].data.data()) +
            bufViews[2]->byteOffset + accessors[2]->byteOffset
          : nullptr,
        hasTangents
          ? reinterpret_cast<const std::byte*>(model.buffers[bufViews[3]->buffer].data.data()) +
            bufViews[3]->byteOffset + accessors[3]->byteOffset
          : nullptr,
        hasTexcoord
          ? reinterpret_cast<const std::byte*>(model.buffers[bufViews[4]->buffer].data.data()) +
            bufViews[4]->byteOffset + accessors[4]->byteOffset
          : nullptr,
      };

      std::array strides{
        bufViews[0]->byteStride != 0
          ? bufViews[0]->byteStride
          : tinygltf::GetComponentSizeInBytes(accessors[0]->componentType) *
            tinygltf::GetNumComponentsInType(accessors[0]->type),
        bufViews[1]->byteStride != 0
          ? bufViews[1]->byteStride
          : tinygltf::GetComponentSizeInBytes(accessors[1]->componentType) *
            tinygltf::GetNumComponentsInType(accessors[1]->type),
        hasNormals ? (bufViews[2]->byteStride != 0
                        ? bufViews[2]->byteStride
                        : tinygltf::GetComponentSizeInBytes(accessors[2]->componentType) *
                          tinygltf::GetNumComponentsInType(accessors[2]->type))
                   : 0,
        hasTangents ? (bufViews[3]->byteStride != 0
                         ? bufViews[3]->byteStride
                         : tinygltf::GetComponentSizeInBytes(accessors[3]->componentType) *
                           tinygltf::GetNumComponentsInType(accessors[3]->type))
                    : 0,
        hasTexcoord ? (bufViews[4]->byteStride != 0
                         ? bufViews[4]->byteStride
                         : tinygltf::GetComponentSizeInBytes(accessors[4]->componentType) *
                           tinygltf::GetNumComponentsInType(accessors[4]->type))
                    : 0,
      };

      for (std::size_t i = 0; i < vertexCount; ++i)
      {
        auto& vtx = result.vertices.emplace_back();
        glm::vec3 pos;
        glm::vec3 normal{0};
        glm::vec4 tangent{0};
        glm::vec2 texcoord{0};
        std::memcpy(&pos, ptrs[1], sizeof(pos));

        // NOTE: it's faster to do a template here with specializations for all combinations than to
        // do ifs at runtime. Also, SIMD should be used. Try implementing this!
        if (hasNormals)
          std::memcpy(&normal, ptrs[2], sizeof(normal));
        if (hasTangents)
          std::memcpy(&tangent, ptrs[3], sizeof(tangent));
        if (hasTexcoord)
          std::memcpy(&texcoord, ptrs[4], sizeof(texcoord));


        vtx.positionAndNormal = glm::vec4(pos, std::bit_cast<float>(encodeNormalized(glm::vec4(normal, 0))));
        vtx.texCoordAndTangentAndPadding =
          glm::vec4(texcoord, std::bit_cast<float>(encodeNormalized(tangent)), 0);


        ptrs[1] += strides[1];
        if (hasNormals)
          ptrs[2] += strides[2];
        if (hasTangents)
          ptrs[3] += strides[3];
        if (hasTexcoord)
          ptrs[4] += strides[4];
      }

      ETNA_VERIFY(bufViews[0]->byteStride == 0);
      const std::size_t indexCount = accessors[0]->count;
      if (accessors[0]->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
      {
        for (std::size_t i = 0; i < indexCount; ++i)
        {
          std::uint16_t index;
          std::memcpy(&index, ptrs[0], sizeof(index));
          result.indices.push_back(index);
          ptrs[0] += 2;
        }
      }
      else if (accessors[0]->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
      {
        const std::size_t lastTotalIndices = result.indices.size();
        result.indices.resize(lastTotalIndices + indexCount);
        std::memcpy(
          result.indices.data() + lastTotalIndices,
          ptrs[0],
          sizeof(result.indices[0]) * indexCount);
      }
    }
  }

  return result;
}

void Baker::changeBuffer(tinygltf::Model& model, BakedMeshes& baked_meshes) {
  auto name = filepath.parent_path();
  name /= filepath.stem();
  auto newFile = name.string() + "_baked.bin";

  std::size_t indicesBufferSize = baked_meshes.indices.size() * sizeof(uint32_t);
  std::size_t vertexBufferSize = baked_meshes.vertices.size() * sizeof(Vertex);

  model.buffers.clear();

  tinygltf::Buffer bakedBuffer{};
  bakedBuffer.name = name.stem().string();
  bakedBuffer.uri = name.filename().string() + "_baked.bin";
  bakedBuffer.data.resize(indicesBufferSize + vertexBufferSize);

  std::memcpy(
    bakedBuffer.data.data(),
    baked_meshes.indices.data(),
    baked_meshes.indices.size() * sizeof(uint32_t));
  std::memcpy(
    bakedBuffer.data.data() + indicesBufferSize,
    baked_meshes.vertices.data(),
    baked_meshes.vertices.size() * sizeof(Vertex));
  model.buffers.push_back(bakedBuffer);
}

void Baker::changeBufferViews(tinygltf::Model& model, BakedMeshes& baked_meshes) {

  tinygltf::BufferView indicesBufferView{};
  indicesBufferView.name = "indices_baked";
  indicesBufferView.buffer = 0;
  indicesBufferView.byteOffset = 0;
  indicesBufferView.byteLength = baked_meshes.indices.size() * sizeof(uint32_t);
  indicesBufferView.byteStride = 0;
  indicesBufferView.target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;

  tinygltf::BufferView vertexBufferView{};
  vertexBufferView.name = "vertices_baked";
  vertexBufferView.buffer = 0;
  vertexBufferView.byteOffset = baked_meshes.indices.size() * sizeof(uint32_t);
  vertexBufferView.byteLength = baked_meshes.vertices.size() * sizeof(Vertex);
  vertexBufferView.byteStride = sizeof(Vertex);
  vertexBufferView.target = TINYGLTF_TARGET_ARRAY_BUFFER;


  model.bufferViews.push_back(indicesBufferView);
  model.bufferViews.push_back(vertexBufferView);
}

void Baker::saveFormatted(tinygltf::Model& model)
{
  auto name = filepath.parent_path();
  name /= filepath.stem();
  auto newFile = name.string() + "_baked.gltf";

  if (!loader.WriteGltfSceneToFile(&model, newFile, false, false, true, false))
  {
    spdlog::error("Error occured when saving formatted file! Location - {}", newFile);
  }
}

uint32_t Baker::encodeNormalized(glm::vec4 normal) const {
  glm::float32_t scale = 127.0;
  uint32_t newX = (lround(normal.x * scale) + 128);
  uint32_t newY = ((lround(normal.y * scale) + 128) << 8);
  uint32_t newZ = ((lround(normal.z * scale) + 128) << 16);
  uint32_t newW = ((lround(normal.w * scale) + 128) << 24);

  uint32_t res = newX | newY | newZ | newW;

  return res;
}