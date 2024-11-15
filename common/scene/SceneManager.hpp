#pragma once

#include <filesystem>

#include <glm/glm.hpp>
#include <tiny_gltf.h>
#include <etna/Buffer.hpp>
#include <etna/BlockingTransferHelper.hpp>
#include <etna/VertexInput.hpp>


// Bounds for each render element
struct Bounds
{
  glm::vec3 origin;
  glm::vec3 extents;
};

// A single render element (relem) corresponds to a single draw call
// of a certain pipeline with specific bindings (including material data)
struct RenderElement
{
  std::uint32_t vertexOffset;
  std::uint32_t indexOffset;
  std::uint32_t indexCount;


  auto operator<=>(const RenderElement& other) const = default;
  // Not implemented!
  // Material* material;
};

struct HashRenderElement
{
  std::size_t operator()(const RenderElement& render_element) const
  {
    return std::hash<std::uint32_t>()(render_element.indexCount) ^
      std::hash<std::uint32_t>()(render_element.indexOffset) ^
      std::hash<std::uint32_t>()(render_element.vertexOffset);
  }
};

// A mesh is a collection of relems. A scene may have the same mesh
// located in several different places, so a scene consists of **instances**,
// not meshes.
struct Mesh
{
  std::uint32_t firstRelem;
  std::uint32_t relemCount;
};

class SceneManager
{
public:
  SceneManager();

  void selectScene(std::filesystem::path path);
  void selectBakedScene(std::filesystem::path path);

  // Every instance is a mesh drawn with a certain transform
  // NOTE: maybe you can pass some additional data through unused matrix entries?
  std::span<const glm::mat4x4> getInstanceMatrices() { return instanceMatrices; }
  std::span<const std::uint32_t> getInstanceMeshes() { return instanceMeshes; }

  // Every mesh is a collection of relems
  std::span<const Mesh> getMeshes() { return meshes; }

  // Every relem is a single draw call
  std::span<const RenderElement> getRenderElements() { return renderElements; }

  std::span<const Bounds> getRenderElementsBounds() { return renderElementsBounds; }

  vk::Buffer getVertexBuffer() { return unifiedVbuf.get(); }
  vk::Buffer getIndexBuffer() { return unifiedIbuf.get(); }

  etna::VertexByteStreamFormatDescription getVertexFormatDescription();

private:
  std::optional<tinygltf::Model> loadModel(std::filesystem::path path);

  struct ProcessedInstances
  {
    std::vector<glm::mat4x4> matrices;
    std::vector<std::uint32_t> meshes;
  };

  ProcessedInstances processInstances(const tinygltf::Model& model) const;

  struct Vertex
  {
    // First 3 floats are position, 4th float is a packed normal
    glm::vec4 positionAndNormal;
    // First 2 floats are tex coords, 3rd is a packed tangent, 4th is padding
    glm::vec4 texCoordAndTangentAndPadding;
  };

  static_assert(sizeof(Vertex) == sizeof(float) * 8);

  struct ProcessedMeshes
  {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<RenderElement> relems;
    std::vector<Mesh> meshes;
    std::vector<Bounds> bounds;
  };
  ProcessedMeshes processMeshes(const tinygltf::Model& model) const;

  struct BakedMeshes
  {
    std::span<const Vertex> vertices;
    std::span<const std::uint32_t> indices;
    std::vector<RenderElement> relems;
    std::vector<Mesh> meshes;
    std::vector<Bounds> bounds;
  };
  BakedMeshes processBakedMeshes(const tinygltf::Model& model) const;
  void uploadData(std::span<const Vertex> vertices, std::span<const std::uint32_t>);

private:
  tinygltf::TinyGLTF loader;
  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
  etna::BlockingTransferHelper transferHelper;

  std::vector<RenderElement> renderElements;
  std::vector<Mesh> meshes;
  std::vector<glm::mat4x4> instanceMatrices;
  std::vector<std::uint32_t> instanceMeshes;
  std::vector<Bounds> renderElementsBounds;

  etna::Buffer unifiedVbuf;
  etna::Buffer unifiedIbuf;
};
