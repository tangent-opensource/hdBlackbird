//  Copyright 2021 Tangent Animation
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied,
//  including without limitation, as related to merchantability and fitness
//  for a particular purpose.
//
//  In no event shall any copyright holder be liable for any damages of any kind
//  arising from the use of this software, whether in contract, tort or otherwise.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include "resourceRegistry.h"
#include "renderDelegate.h"
#include "renderParam.h"

#include <pxr/base/work/loops.h>
#include <pxr/usd/sdf/path.h>

#include <render/geometry.h>
#include <render/object.h>
#include <render/scene.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
struct HdCyclesSessionAutoPause {
    explicit HdCyclesSessionAutoPause(ccl::Session* s)
        : session(s)
    {
        session->set_pause(true);
    }

    ~HdCyclesSessionAutoPause() { session->set_pause(false); }

    ccl::Session* session;
};
}  // namespace

void
HdCyclesResourceRegistry::_Commit()
{
    //
    // *** WARNING ***
    //
    // This function is under heavy wip. In ideal situation committing all resources to cycles should happen in one
    // place only.

    auto session = m_renderDelegate->GetCyclesRenderParam()->GetCyclesSession();
    auto scene   = m_renderDelegate->GetCyclesRenderParam()->GetCyclesScene();

    // Pause rendering for committing
    HdCyclesSessionAutoPause session_auto_pause { session };

    // State used to control session/scene update reset
    std::atomic_size_t num_new_objects { 0 };
    std::atomic_size_t num_new_geometries { 0 };
    std::atomic_size_t num_new_sources { 0 };

    // scene must be locked before any modifications
    ccl::thread_scoped_lock scene_lock { scene->mutex };

    //
    // * bind lights
    //

    //
    // * bind shaders
    //

    //
    // * bind objects and geometries to the scene
    //
    for (auto& object_source : m_objects) {  // TODO: preallocate objects
        HdCyclesObjectSource* source_ptr = object_source.second.value.get();

        if (!source_ptr->IsValid()) {
            continue;
        }

        if (source_ptr->IsResolved()) {
            continue;
        }

        // resolve and bind
        source_ptr->Resolve();

        ccl::Object* object = source_ptr->GetObject();
        if (!object) {
            continue;
        }
        scene->objects.push_back(object);
        object->tag_update(scene);
        ++num_new_objects;

        ccl::Geometry* geometry = object->geometry;
        if (!geometry) {
            continue;
        }
        scene->geometry.push_back(geometry);
        geometry->tag_update(scene, true);  // new object bvh has to be rebuild
        ++num_new_geometries;
    }

    //
    // * commit all pending object sources
    //
    using ValueType = HdInstanceRegistry<HdCyclesObjectSourceSharedPtr>::const_iterator::value_type;
    WorkParallelForEach(m_objects.begin(), m_objects.end(), [&num_new_sources, scene](const ValueType& object_source) {
        // resolve per object
        size_t num_resolved_sources = object_source.second.value->ResolvePendingSources();
        if (num_resolved_sources > 0) {
            ++num_new_sources;
            object_source.second.value->GetObject()->tag_update(scene);
        }
    });

    //
    // * notify cycles about the changes
    //
    std::atomic_bool requires_reset { false };

    if (num_new_objects > 0) {
        scene->object_manager->tag_update(scene);
        requires_reset = true;
    }

    if (num_new_geometries > 0) {
        scene->geometry_manager->tag_update(scene);
        requires_reset = true;
    }

    if (num_new_sources > 0) {
        requires_reset = true;
    }

    //
    // * restart if necessary
    //
    if (requires_reset) {
        m_renderDelegate->GetCyclesRenderParam()->CyclesReset(true);
    }
}


void
HdCyclesResourceRegistry::_GarbageCollectObjectAndGeometry()
{
    auto scene = m_renderDelegate->GetCyclesRenderParam()->GetCyclesScene();

    // Design note:
    // Unique instances of shared pointer are considered not used in the scene and should be detached from the scene
    // before removal. Instead of removing objects from the scene during RPrim destructor or Finalize calls,
    // we group them into unordered set of pointers, then we sweep through all objects once and remove those that are
    // unique.

    std::unordered_set<const ccl::Object*> unique_objects;
    std::unordered_set<const ccl::Geometry*> unique_geometries;

    //
    // * collect unique objects and geometries
    //
    for (const auto& object_instance : m_objects) {
        if (!object_instance.second.value.unique()) {
            continue;
        }

        const ccl::Object* object = object_instance.second.value->GetObject();
        if (!object) {
            continue;
        }

        // Mark for unbinding
        unique_objects.insert(object);
        const ccl::Geometry* geometry = object->geometry;
        if (geometry) {
            unique_geometries.insert(geometry);
        }
    }

    //
    // * unbind objects and geometries
    //
    if (unique_objects.empty()) {
        return;
    }

    // remove geometries
    for (auto it = scene->geometry.begin(); it != scene->geometry.end();) {
        if (unique_geometries.find(*it) != unique_geometries.end()) {
            it = scene->geometry.erase(it);
        } else {
            ++it;
        }
    }

    // remove objects
    for (auto it = scene->objects.begin(); it != scene->objects.end();) {
        if (unique_objects.find(*it) != unique_objects.end()) {
            it = scene->objects.erase(it);
        } else {
            ++it;
        }
    }
}

void
HdCyclesResourceRegistry::_GarbageCollect()
{
    auto scene = m_renderDelegate->GetCyclesRenderParam()->GetCyclesScene();
    ccl::thread_scoped_lock scene_lock { scene->mutex };

    // Design note:
    // One might think that following OOP pattern would be the best choice of action, and deletion of the objects should
    // happen in HdCyclesObjectSource. It turns out it's better to collect all unique objects and remove them in one
    // iteration.

    //
    // * Unbind unique instances of Geometry and Object from the Scene
    //
    {
        _GarbageCollectObjectAndGeometry();
    }

    //
    // * delete unique objects
    //
    {
        m_objects.GarbageCollect();
    }
}

HdInstance<HdCyclesObjectSourceSharedPtr>
HdCyclesResourceRegistry::GetObjectInstance(const SdfPath& id)
{
    return m_objects.GetInstance(id.GetHash());
}

HdCyclesResourceRegistry::~HdCyclesResourceRegistry() { _GarbageCollect(); }
