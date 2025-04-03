#pragma once

#include <etna/OneShotCmdMgr.hpp>
#include <etna/Buffer.hpp>
#include <etna/Image.hpp>

class RenderUtility
{
public:
  static void localCopyBufferToImage(
    etna::OneShotCmdMgr& one_shot_cmd_mgr,
    const etna::Buffer& buffer,
    const etna::Image& image,
    uint32_t layer_count);
  static void generateMipmapsVkStyle(
    etna::OneShotCmdMgr& one_shot_cmd_mgr,
    const etna::Image& image,
    uint32_t mip_levels,
    uint32_t layer_count);
};
