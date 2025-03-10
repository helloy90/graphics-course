#pragma once

#include <glm/gtc/type_precision.hpp>
#include <vector>

#include <etna/Assert.hpp>

#include "Texture2D.hpp"
#include "Material.hpp"


template <class Res>
  requires(requires { typename Res::Id; })
class ResourceManager
{
public:
  ResourceManager() = default;
  ~ResourceManager() { clear(); }

  ResourceManager(const ResourceManager&) = delete;
  ResourceManager& operator=(const ResourceManager&) = delete;

  Res::Id loadResource(const char* name, Res resource)
  {
    if (names.find(name) != names.end())
    {
      ETNA_PANIC("Resource {} redefenition, for now is not supported", name);
    }
    auto resId = static_cast<Res::Id>(storage.size());
    storage.emplace_back(std::move(resource));
    names[name] = resId;
    return resId;
  }

  void reserve(std::size_t size)
  {
    storage.reserve(size);
    names.reserve(size);
  }

  Res::Id tryGetResourceId(const char* name)
  {
    auto it = names.find(name);
    if (it == names.end())
    {
      return Res::Id::Invalid;
    }
    return it->second;
  }

  Res::Id getResourceId(const char* name)
  {
    auto it = names.find(name);
    if (it == names.end())
    {
      // maybe add recovery later
      ETNA_PANIC("Resource {} not found", name);
    }
    return it->second;
  }

  Res getResource(Res::Id id)
  {
    if (id >= storage.size())
    {
      // maybe add recovery later
      ETNA_PANIC("Invalid resource id {}", id);
    }
    return storage[static_cast<std::underlying_type_t<typename Res::Id>>(id)];
  }

  const Res& getResource(Res::Id id) const
  {
    if (static_cast<uint32_t>(id) >= storage.size())
    {
      // maybe add recovery later
      ETNA_PANIC("Invalid resource id {}", static_cast<uint32_t>(id));
    }
    return storage[static_cast<std::underlying_type_t<typename Res::Id>>(id)];
  }

  Res getResource(const char* name) { return getResource(getResourceId(name)); }
  const Res& getResource(const char* name) const { return getResource(getResourceId(name)); }

  void clear()
  {
    storage.clear();
    names.clear();
  }

private:
  std::vector<Res> storage;
  std::unordered_map<std::string, typename Res::Id> names;
};

using Texture2DManager = ResourceManager<Texture2D>;
using MaterialManager = ResourceManager<Material>;
