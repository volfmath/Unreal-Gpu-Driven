



# Unreal-Gpu-Driven

Test code for gpu driven in unreal

- [x] Support ISM
  - [x] Indirect Draw Buffer Construct
  - [x] Dynamic Indirect Draw, Filter shadows, Virtual Textures, Dithered material
  - [x] LocalVertex Factory ManualVertexFetch
  - [x] Compute Shader for Frustum Culling
  - [x] Hiz Occlusion Culling
  - [x] Calculate Lod and update indirectDraw FirstInstance
  - [x] Delete shadow variable
- [x] Support HISM
  - [x] Construct the Cluster
  - [x] Rewrite GetDynamicElement
  - [x] Distance Culling
  - [x] HISM Lod Calculate，Support different Scalebound

Optimazation：

- [x] Delete the Occlusion Query for HISM
- [x] Support manual label check
- [x] Dynamic enable and disable
- [x] Dynamic Cluster Split
- [ ] Display culling parameters
- [ ] Support the use of  different Cluster coefficients for each Instance

Test:

- [x] IOS
- [x] Android

Metal：

- [ ] Use FirstInstance parameters instead of creating Buffer

Opengl es:

- [x] Support StructureBuffer
- [ ] Fix the problem that the format does not support RGBA16_SNORM when use LightMap

Vulkan：

- [ ] Unreal performance issues