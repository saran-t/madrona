#include <madrona/physics_assets.hpp>
#include <madrona/cuda_utils.hpp>


namespace madrona {
namespace phys {

struct PhysicsLoader::Impl {
    RigidBodyMetadata *metadatas;
    math::AABB *aabbs;
    CollisionPrimitive *primitives;

    // For half edge meshes
    geometry::PolygonData *polygonDatas;
    geometry::EdgeData *edgeDatas;
    geometry::HalfEdge *halfEdges;
    math::Vector3 *vertices;

    CountT polygonCount;
    CountT edgeCount;
    CountT halfEdgeCount;
    CountT vertexCount;

    ObjectManager *mgr;
    CountT curLoadedObjs;
    CountT maxObjs;
    StorageType storageType;

    static Impl * init(StorageType storage_type, CountT max_objects)
    {
        CountT max_vertices_per_object = 100;
        CountT max_polygons_per_object = 100;
        CountT max_edges_per_object = 100;
        CountT max_half_edges_per_object = 100;

        size_t num_metadata_bytes =
            sizeof(RigidBodyMetadata) * max_objects;

        size_t num_aabb_bytes =
            sizeof(math::AABB) * max_objects;

        size_t num_primitive_bytes =
            sizeof(CollisionPrimitive) * max_objects;

        size_t num_vertices_bytes =
            sizeof(math::Vector3) * max_objects * max_vertices_per_object; 

        size_t num_polygon_bytes =
            sizeof(geometry::PolygonData) * max_objects * max_polygons_per_object; 

        size_t num_edges_bytes =
            sizeof(geometry::EdgeData) * max_objects * max_edges_per_object; 

        size_t num_half_edges_bytes =
            sizeof(geometry::HalfEdge) * max_objects * max_half_edges_per_object; 

        RigidBodyMetadata *metadata_ptr;
        math::AABB *aabb_ptr;
        CollisionPrimitive *primitives;
        geometry::PolygonData *polygonDatas_ptr;
        geometry::EdgeData *edgeDatas_ptr;
        geometry::HalfEdge *halfEdges_ptr;
        math::Vector3 *vertices_ptr;

        ObjectManager *mgr;

        switch (storage_type) {
        case StorageType::CPU: {
            metadata_ptr =
                (RigidBodyMetadata *)malloc(num_metadata_bytes);
            aabb_ptr = (math::AABB *)malloc(num_aabb_bytes);
            primitives = (CollisionPrimitive *)malloc(num_primitive_bytes);

            polygonDatas_ptr = (geometry::PolygonData *)malloc(num_polygon_bytes);
            edgeDatas_ptr = (geometry::EdgeData *)malloc(num_edges_bytes);
            halfEdges_ptr = (geometry::HalfEdge *)malloc(num_half_edges_bytes);
            vertices_ptr = (math::Vector3 *)malloc(num_vertices_bytes);

            mgr = new ObjectManager {
                metadata_ptr,
                aabb_ptr,
                primitives,
                polygonDatas_ptr,
                edgeDatas_ptr,
                halfEdges_ptr,
                vertices_ptr
            };
        } break;
        case StorageType::CUDA: {
            metadata_ptr =
                (RigidBodyMetadata *)cu::allocGPU(num_metadata_bytes);
            aabb_ptr = (math::AABB *)cu::allocGPU(num_aabb_bytes);
            primitives =
                (CollisionPrimitive *)cu::allocGPU(num_primitive_bytes);

            polygonDatas_ptr = (geometry::PolygonData *)cu::allocGPU(num_polygon_bytes);
            edgeDatas_ptr = (geometry::EdgeData *)cu::allocGPU(num_edges_bytes);
            halfEdges_ptr = (geometry::HalfEdge *)cu::allocGPU(num_half_edges_bytes);
            vertices_ptr = (math::Vector3 *)cu::allocGPU(num_vertices_bytes);

            mgr = (ObjectManager *)cu::allocGPU(sizeof(ObjectManager));

            ObjectManager local {
                metadata_ptr,
                aabb_ptr,
                primitives,
                polygonDatas_ptr,
                edgeDatas_ptr,
                halfEdges_ptr,
                vertices_ptr
            };

            REQ_CUDA(cudaMemcpy(mgr, &local, sizeof(ObjectManager),
                                cudaMemcpyHostToDevice));
        } break;
        default: __builtin_unreachable();
        }

        return new Impl {
            .metadatas = metadata_ptr,
            .aabbs = aabb_ptr,
            .primitives = primitives,
            .polygonDatas = polygonDatas_ptr,
            .edgeDatas = edgeDatas_ptr,
            .halfEdges = halfEdges_ptr,
            .vertices = vertices_ptr,
            .polygonCount = 0,
            .edgeCount = 0,
            .halfEdgeCount = 0,
            .vertexCount = 0,
            .mgr = mgr,
            .curLoadedObjs = 0,
            .maxObjs = max_objects,
            .storageType = storage_type,
        };
    }
};

PhysicsLoader::PhysicsLoader(StorageType storage_type, CountT max_objects)
    : impl_(Impl::init(storage_type, max_objects))
{}

PhysicsLoader::~PhysicsLoader()
{
    if (impl_ == nullptr) {
        return;
    }

    switch (impl_->storageType) {
    case StorageType::CPU: {
        delete impl_->mgr;
        free(impl_->primitives);
        free(impl_->aabbs);
        free(impl_->metadatas);
        free(impl_->polygonDatas);
        free(impl_->edgeDatas);
        free(impl_->halfEdges);
        free(impl_->vertices);
    } break;
    case StorageType::CUDA: {
        cu::deallocGPU(impl_->mgr);
        cu::deallocGPU(impl_->primitives);
        cu::deallocGPU(impl_->aabbs);
        cu::deallocGPU(impl_->metadatas);
        cu::deallocGPU(impl_->polygonDatas);
        cu::deallocGPU(impl_->edgeDatas);
        cu::deallocGPU(impl_->halfEdges);
        cu::deallocGPU(impl_->vertices);
    } break;
    }
}

PhysicsLoader::PhysicsLoader(PhysicsLoader &&o) = default;

CountT PhysicsLoader::loadObjects(const RigidBodyMetadata *metadatas,
                                  const math::AABB *aabbs,
                                  const CollisionPrimitive *primitives_original,
                                  CountT num_objs)
{
    CountT cur_offset = impl_->curLoadedObjs;
    impl_->curLoadedObjs += num_objs;
    assert(impl_->curLoadedObjs <= impl_->maxObjs);

    size_t num_metadata_bytes = sizeof(RigidBodyMetadata) * num_objs;
    size_t num_aabb_bytes = sizeof(math::AABB) * num_objs;
    size_t num_prim_bytes = sizeof(CollisionPrimitive) * num_objs;

    RigidBodyMetadata *metadatas_dst = &impl_->metadatas[cur_offset];
    math::AABB *aabbs_dst = &impl_->aabbs[cur_offset];
    CollisionPrimitive *prims_dst = &impl_->primitives[cur_offset];

    CollisionPrimitive *primitives = (CollisionPrimitive *)malloc(sizeof(CollisionPrimitive) * num_objs);
    memcpy(primitives, primitives_original, sizeof(CollisionPrimitive) * num_objs);

    switch (impl_->storageType) {
    case StorageType::CPU: {
        for (int i = 0; i < num_objs; ++i) {
            if (primitives[i].type == CollisionPrimitive::Type::Hull) {
                auto &hEdgeMesh = primitives[i].hull.halfEdgeMesh;
                memcpy(
                    impl_->polygonDatas + impl_->polygonCount,
                    hEdgeMesh.mPolygons,
                    sizeof(geometry::PolygonData) * hEdgeMesh.mPolygonCount);
                hEdgeMesh.mPolygons = impl_->polygonDatas + impl_->polygonCount;
                impl_->polygonCount += hEdgeMesh.mPolygonCount;

                memcpy(
                    impl_->edgeDatas + impl_->edgeCount,
                    hEdgeMesh.mEdges,
                    sizeof(geometry::EdgeData) * hEdgeMesh.mEdgeCount);
                hEdgeMesh.mEdges = impl_->edgeDatas + impl_->edgeCount;
                impl_->edgeCount += hEdgeMesh.mEdgeCount;

                memcpy(
                    impl_->halfEdges + impl_->halfEdgeCount,
                    hEdgeMesh.mHalfEdges,
                    sizeof(geometry::HalfEdge) * hEdgeMesh.mHalfEdgeCount);
                hEdgeMesh.mHalfEdges = impl_->halfEdges + impl_->halfEdgeCount;
                impl_->halfEdgeCount += hEdgeMesh.mHalfEdgeCount;

                memcpy(
                    impl_->vertices + impl_->vertexCount,
                    hEdgeMesh.mVertices,
                    sizeof(math::Vector3) * hEdgeMesh.mVertexCount);
                hEdgeMesh.mVertices = impl_->vertices + impl_->vertexCount;
                impl_->vertexCount += hEdgeMesh.mVertexCount;
            }
        }

        memcpy(metadatas_dst, metadatas, num_metadata_bytes);
        memcpy(aabbs_dst, aabbs, num_aabb_bytes);
        memcpy(prims_dst, primitives, num_prim_bytes);
    } break;
    case StorageType::CUDA: {
        for (int i = 0; i < num_objs; ++i) {
            if (primitives[i].type == CollisionPrimitive::Type::Hull) {
                auto &hEdgeMesh = primitives[i].hull.halfEdgeMesh;

                printf("mPolygonCount = %d; mPolygons = %p\n", hEdgeMesh.mPolygonCount, hEdgeMesh.mPolygons);
                cudaMemcpy(
                    impl_->polygonDatas + impl_->polygonCount,
                    hEdgeMesh.mPolygons,
                    sizeof(geometry::PolygonData) * hEdgeMesh.mPolygonCount,
                    cudaMemcpyHostToDevice);
                hEdgeMesh.mPolygons = impl_->polygonDatas + impl_->polygonCount;
                impl_->polygonCount += hEdgeMesh.mPolygonCount;
#if 0
                printf("%d %d %d %d %d %d\n",
                        hEdgeMesh.mPolygons[0],
                        hEdgeMesh.mPolygons[1],
                        hEdgeMesh.mPolygons[2],
                        hEdgeMesh.mPolygons[3],
                        hEdgeMesh.mPolygons[4],
                        hEdgeMesh.mPolygons[5]);
#endif

                printf("mEdgeCount = %d; mEdges = %p\n", hEdgeMesh.mEdgeCount, hEdgeMesh.mEdges);
                cudaMemcpy(
                    impl_->edgeDatas + impl_->edgeCount,
                    hEdgeMesh.mEdges,
                    sizeof(geometry::EdgeData) * hEdgeMesh.mEdgeCount,
                    cudaMemcpyHostToDevice);
                hEdgeMesh.mEdges = impl_->edgeDatas + impl_->edgeCount;
                impl_->edgeCount += hEdgeMesh.mEdgeCount;

                printf("mHalfEdgeCount = %d; mHalfEdges = %p\n", hEdgeMesh.mHalfEdgeCount, hEdgeMesh.mHalfEdges);
                cudaMemcpy(
                    impl_->halfEdges + impl_->halfEdgeCount,
                    hEdgeMesh.mHalfEdges,
                    sizeof(geometry::HalfEdge) * hEdgeMesh.mHalfEdgeCount,
                    cudaMemcpyHostToDevice);
                hEdgeMesh.mHalfEdges = impl_->halfEdges + impl_->halfEdgeCount;
                impl_->halfEdgeCount += hEdgeMesh.mHalfEdgeCount;

                cudaMemcpy(
                    impl_->vertices + impl_->vertexCount,
                    hEdgeMesh.mVertices,
                    sizeof(math::Vector3) * hEdgeMesh.mVertexCount,
                    cudaMemcpyHostToDevice);
                hEdgeMesh.mVertices = impl_->vertices + impl_->vertexCount;
                impl_->vertexCount += hEdgeMesh.mVertexCount;
            }
        }

        cudaMemcpy(metadatas_dst, metadatas, num_metadata_bytes,
                   cudaMemcpyHostToDevice);
        cudaMemcpy(aabbs_dst, aabbs, num_aabb_bytes,
                   cudaMemcpyHostToDevice);
        cudaMemcpy(prims_dst, primitives, num_prim_bytes,
                   cudaMemcpyHostToDevice);
    } break;
    default: __builtin_unreachable();
    }

    free(primitives);

    return cur_offset;
}

ObjectManager & PhysicsLoader::getObjectManager()
{
    return *impl_->mgr;
}

}
}

