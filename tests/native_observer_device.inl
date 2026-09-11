// Type-checked adapter for the actual ReShade init-device callback in CPU/WARP
// fixtures. The API getters expose the supplied native object; every unrelated
// virtual aborts instead of simulating graphics work.
#include <cstdlib>
namespace reshade::api {
struct NativeObserverTestDevice final : device {
    ID3D12Device *native;
    explicit NativeObserverTestDevice(ID3D12Device *value):native(value){}
    uint64_t get_native() const override { return reinterpret_cast<uint64_t>(native); }
        void get_private_data(const uint8_t guid[16], uint64_t *data) const override { std::abort(); }
        void set_private_data(const uint8_t guid[16], const uint64_t data)  override { std::abort(); }
    device_api get_api() const override { return device_api::d3d12; }
    bool check_capability(device_caps capability) const override { std::abort(); }
    bool check_format_support(format format, resource_usage usage) const override { std::abort(); }
    bool create_sampler(const sampler_desc &desc, sampler *out_sampler) override { std::abort(); }
    void destroy_sampler(sampler sampler) override { std::abort(); }
    bool create_resource(const resource_desc &desc, const subresource_data *initial_data, resource_usage initial_state, resource *out_resource, void **shared_handle = nullptr) override { std::abort(); }
    void destroy_resource(resource resource) override { std::abort(); }
    resource_desc get_resource_desc(resource resource) const override { std::abort(); }
    bool create_resource_view(resource resource, resource_usage usage_type, const resource_view_desc &desc, resource_view *out_view) override { std::abort(); }
    void destroy_resource_view(resource_view view) override { std::abort(); }
    resource get_resource_from_view(resource_view view) const override { std::abort(); }
    resource_view_desc get_resource_view_desc(resource_view view) const override { std::abort(); }
    bool map_buffer_region(resource resource, uint64_t offset, uint64_t size, map_access access, void **out_data) override { std::abort(); }
    void unmap_buffer_region(resource resource) override { std::abort(); }
    bool map_texture_region(resource resource, uint32_t subresource, const subresource_box *box, map_access access, subresource_data *out_data) override { std::abort(); }
    void unmap_texture_region(resource resource, uint32_t subresource) override { std::abort(); }
    void update_buffer_region(const void *data, resource dest, uint64_t dest_offset, uint64_t size) override { std::abort(); }
    void update_texture_region(const subresource_data &data, resource dest, uint32_t dest_subresource, const subresource_box *dest_box = nullptr) override { std::abort(); }
    bool create_pipeline(pipeline_layout layout, uint32_t subobject_count, const pipeline_subobject *subobjects, pipeline *out_pipeline) override { std::abort(); }
    void destroy_pipeline(pipeline pipeline) override { std::abort(); }
    bool create_pipeline_layout(uint32_t param_count, const pipeline_layout_param *params, pipeline_layout *out_layout) override { std::abort(); }
    void destroy_pipeline_layout(pipeline_layout layout) override { std::abort(); }
    bool allocate_descriptor_tables(uint32_t count, pipeline_layout layout, uint32_t param, descriptor_table *out_tables) override { std::abort(); }
    void free_descriptor_tables(uint32_t count, const descriptor_table *tables) override { std::abort(); }
    void get_descriptor_heap_offset(descriptor_table table, uint32_t binding, uint32_t array_offset, descriptor_heap *out_heap, uint32_t *out_offset) const override { std::abort(); }
    void copy_descriptor_tables(uint32_t count, const descriptor_table_copy *copies) override { std::abort(); }
    void update_descriptor_tables(uint32_t count, const descriptor_table_update *updates) override { std::abort(); }
    bool create_query_heap(query_type type, uint32_t count, query_heap *out_heap) override { std::abort(); }
    void destroy_query_heap(query_heap heap) override { std::abort(); }
    bool get_query_heap_results(query_heap heap, uint32_t first, uint32_t count, void *results, uint32_t stride) override { std::abort(); }
    void set_resource_name(resource resource, const char *name) override { std::abort(); }
    void set_resource_view_name(resource_view view, const char *name) override { std::abort(); }
    bool create_fence(uint64_t initial_value, fence_flags flags, fence *out_fence, void **shared_handle = nullptr) override { std::abort(); }
    void destroy_fence(fence fence) override { std::abort(); }
    uint64_t get_completed_fence_value(fence fence) const override { std::abort(); }
    bool wait(fence fence, uint64_t value, uint64_t timeout = UINT64_MAX) override { std::abort(); }
    bool signal(fence fence, uint64_t value) override { std::abort(); }
    bool get_property(device_properties property, void *data) const override { std::abort(); }
    uint64_t get_resource_view_gpu_address(resource_view view) const override { std::abort(); }
    void get_acceleration_structure_size(acceleration_structure_type type, acceleration_structure_build_flags flags, uint32_t input_count, const acceleration_structure_build_input *inputs, uint64_t *out_size, uint64_t *out_build_scratch_size, uint64_t *out_update_scratch_size) const override { std::abort(); }
    bool get_pipeline_shader_group_handles(pipeline pipeline, uint32_t first, uint32_t count, void *out_handles) override { std::abort(); }
};
}
