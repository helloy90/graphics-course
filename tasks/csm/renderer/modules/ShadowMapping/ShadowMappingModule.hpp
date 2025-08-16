#pragma once


class ShadowMappingModule
{
public:
  ShadowMappingModule();

  void allocateResources();
  void loadShaders();
  void setupPipelines(vk::Format shadow_map_format);
  void execute();

private:
};
