/*
 *  Copyright 2024 Diligent Graphics LLC
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#include "HnGeometryPool.hpp"
#include "HnTokens.hpp"

#include "GraphicsTypesX.hpp"
#include "GLTFResourceManager.hpp"

#include "pxr/base/tf/token.h"
#include "pxr/base/gf/vec3i.h"
#include "pxr/base/gf/vec2i.h"
#include "pxr/base/vt/array.h"
#include "pxr/imaging/hd/tokens.h"

namespace Diligent
{

namespace USD
{

HnGeometryPool::HnGeometryPool(IRenderDevice*         pDevice,
                               GLTF::ResourceManager& ResMgr,
                               bool                   UseVertexPool,
                               bool                   UseIndexPool) :
    m_pDevice{pDevice},
    m_ResMgr{ResMgr},
    m_UseVertexPool{UseVertexPool},
    m_UseIndexPool{UseIndexPool},
    m_VertexCache{/*NumRequestsToPurge = */ 128},
    m_IndexCache{/*NumRequestsToPurge = */ 128}
{
}

HnGeometryPool::~HnGeometryPool()
{
}

class HnGeometryPool::VertexData final
{
public:
    using VertexStreamHashesType = std::map<pxr::TfToken, size_t>;

    VertexData(std::string                   Name,
               const BufferSourcesMapType&   Sources,
               const VertexStreamHashesType& Hashes,
               GLTF::ResourceManager*        pResMgr,
               std::shared_ptr<VertexData>   ExistingData) :
        m_Name{std::move(Name)},
        m_StagingData{std::make_unique<StagingData>()}
    {
        if (Sources.empty())
        {
            UNEXPECTED("No vertex data sources provided");
            return;
        }

        m_NumVertices = static_cast<Uint32>(Sources.begin()->second->GetNumElements());
        for (const auto& source_it : Sources)
        {
            const pxr::TfToken&                         SourceName = source_it.first;
            const std::shared_ptr<pxr::HdBufferSource>& Source     = source_it.second;

            VERIFY(m_NumVertices == source_it.second->GetNumElements(), "The number of elements in buffer source '", SourceName,
                   "' does not match the number of vertices (", m_NumVertices, ")");

            const pxr::HdTupleType ElementType = Source->GetTupleType();
            const size_t           ElementSize = HdDataSizeOfType(ElementType.type) * ElementType.count;
            if (SourceName == pxr::HdTokens->points)
                VERIFY(ElementType.type == pxr::HdTypeFloatVec3 && ElementType.count == 1, "Unexpected vertex element type");
            else if (SourceName == pxr::HdTokens->normals)
                VERIFY(ElementType.type == pxr::HdTypeFloatVec3 && ElementType.count == 1, "Unexpected normal element type");
            else if (SourceName == pxr::HdTokens->displayColor)
                VERIFY(ElementType.type == pxr::HdTypeFloatVec3 && ElementType.count == 1, "Unexpected vertex color element type");
            else if (SourceName == HnTokens->joints)
                VERIFY(ElementType.type == pxr::HdTypeFloatVec4 && ElementType.count == 2, "Unexpected joints element type");

            VERIFY(m_Streams.find(SourceName) == m_Streams.end(), "Duplicate vertex data source '", SourceName, "'");
            VertexStream& Stream = m_Streams[SourceName];
            Stream.ElementSize   = ElementSize;

            auto hash_it = Hashes.find(SourceName);
            if (hash_it != Hashes.end())
                Stream.Hash = hash_it->second;
            else
                UNEXPECTED("Failed to find hash for vertex stream '", SourceName, "'. This is a bug.");

            m_StagingData->Sources[SourceName].Source = Source;
        }

        // Add sources from the existing data that are not present in the new sources
        if (ExistingData)
        {
            for (auto& stream_it : ExistingData->m_Streams)
            {
                const pxr::TfToken& SourceName     = stream_it.first;
                VertexStream&       ExistingStream = stream_it.second;

                const auto new_stream_it = m_Streams.find(SourceName);
                if (new_stream_it == m_Streams.end())
                {
                    VertexStream& Stream = m_Streams[SourceName];
                    Stream.ElementSize   = ExistingStream.ElementSize;

                    auto hash_it = Hashes.find(SourceName);
                    if (hash_it != Hashes.end())
                        Stream.Hash = hash_it->second;
                    else
                        UNEXPECTED("Failed to find hash for vertex stream '", SourceName, "'. This is a bug.");

                    VERIFY_EXPR(ExistingStream.Buffer);
                    m_StagingData->Sources[SourceName].Buffer = ExistingStream.Buffer;

                    if (!m_StagingData->ExistingData)
                        m_StagingData->ExistingData = ExistingData;
                }
            }
        }

        if (pResMgr != nullptr)
        {
            GLTF::ResourceManager::VertexLayoutKey VtxKey;
            VtxKey.Elements.reserve(m_Streams.size());
            for (auto& stream_it : m_Streams)
            {
                VertexStream& Stream = stream_it.second;
                Stream.PoolIndex     = static_cast<Uint32>(VtxKey.Elements.size());
                VtxKey.Elements.emplace_back(static_cast<Uint32>(Stream.ElementSize), BIND_VERTEX_BUFFER);
            }

            m_PoolAllocation = pResMgr->AllocateVertices(VtxKey, m_NumVertices);
            VERIFY_EXPR(m_PoolAllocation);
        }
    }

    IBuffer* GetBuffer(const pxr::TfToken& Name) const
    {
        auto it = m_Streams.find(Name);
        return it != m_Streams.end() ? it->second.Buffer : nullptr;
    }

    Uint32 GetNumVertices() const
    {
        return m_NumVertices;
    }

    Uint32 GetStartVertex() const
    {
        return m_PoolAllocation ? m_PoolAllocation->GetStartVertex() : 0;
    }

    void Update(IRenderDevice*  pDevice,
                IDeviceContext* pContext)
    {
        if (!m_StagingData)
        {
            UNEXPECTED("No staging data. This may indicate the Update() method is called more than once, which is a bug.");
            return;
        }

        for (auto& stream_it : m_Streams)
        {
            const pxr::TfToken& StreamName = stream_it.first;
            VertexStream&       Stream     = stream_it.second;

            auto source_it = m_StagingData->Sources.find(StreamName);
            if (source_it == m_StagingData->Sources.end())
            {
                UNEXPECTED("Failed to find staging data for vertex stream '", StreamName, "'. This is a bug.");
                continue;
            }

            const Uint32 DataSize = static_cast<Uint32>(m_NumVertices * Stream.ElementSize);
            if (m_PoolAllocation)
            {
                VERIFY(m_PoolAllocation->GetVertexCount() == m_NumVertices, "Unexpected number of vertices in the pool allocation.");
                Stream.Buffer = m_PoolAllocation->GetBuffer(Stream.PoolIndex);
            }
            else
            {
                const auto BufferName = m_Name + " - " + StreamName.GetString();
                BufferDesc Desc{
                    BufferName.c_str(),
                    DataSize,
                    BIND_VERTEX_BUFFER,
                    USAGE_DEFAULT,
                };
                pDevice->CreateBuffer(Desc, nullptr, &Stream.Buffer);
            }

            if (const pxr::HdBufferSource* pSource = source_it->second.Source.get())
            {
                VERIFY(pSource->GetNumElements() == m_NumVertices, "Unexpected number of elements in vertex data source ", StreamName);
                pContext->UpdateBuffer(Stream.Buffer, GetStartVertex() * Stream.ElementSize, DataSize, pSource->GetData(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            }
            else if (IBuffer* pSrcBuffer = source_it->second.Buffer)
            {
                pContext->CopyBuffer(
                    Stream.Buffer,
                    m_StagingData->ExistingData->GetStartVertex() * Stream.ElementSize,
                    RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                    Stream.Buffer,
                    GetStartVertex() * Stream.ElementSize,
                    DataSize,
                    RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            }
            else
            {
                UNEXPECTED("No source data provided for vertex stream '", StreamName, "'");
            }

            if (!m_PoolAllocation)
            {
                StateTransitionDesc Barrier{Stream.Buffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE};
                pContext->TransitionResourceStates(1, &Barrier);
            }
        }

        m_StagingData.reset();
    }

    bool IsCompatible(const BufferSourcesMapType& Sources) const
    {
        if (Sources.empty())
        {
            UNEXPECTED("No vertex data sources provided");
            return false;
        }

        for (const auto& source_it : Sources)
        {
            const pxr::TfToken&                         SourceName = source_it.first;
            const std::shared_ptr<pxr::HdBufferSource>& Source     = source_it.second;

            if (Source->GetNumElements() != m_NumVertices)
                return false;

            const auto stream_it = m_Streams.find(SourceName);
            if (stream_it == m_Streams.end())
                return false;

            const pxr::HdTupleType ElementType = Source->GetTupleType();
            const size_t           ElementSize = HdDataSizeOfType(ElementType.type) * ElementType.count;
            if (ElementSize != stream_it->second.ElementSize)
                return false;
        }

        return true;
    }

    static VertexStreamHashesType ComputeVertexStreamHashes(const BufferSourcesMapType& Sources, const VertexData* pData)
    {
        VertexStreamHashesType Hashes;
        if (pData != nullptr)
        {
            VERIFY_EXPR(pData->IsCompatible(Sources));
            for (const auto& it : pData->m_Streams)
            {
                const pxr::TfToken& StreamName = it.first;
                const VertexStream& Stream     = it.second;
                Hashes[StreamName]             = Stream.Hash;
            }
        }

        for (const auto& source_it : Sources)
        {
            Hashes[source_it.first] = source_it.second->ComputeHash();
        }

        return Hashes;
    }

    size_t GetStreamCount() const
    {
        return m_Streams.size();
    }

private:
    const std::string m_Name;

    Uint32 m_NumVertices = 0;

    RefCntAutoPtr<IVertexPoolAllocation> m_PoolAllocation;

    struct VertexStream
    {
        // pool element index (e.g. "normals" -> 0, "points" -> 1, etc.)
        Uint32 PoolIndex;

        RefCntAutoPtr<IBuffer> Buffer;

        size_t ElementSize = 0;

        size_t Hash = 0;
    };
    // Keep streams sorted by name to ensure deterministic order
    std::map<pxr::TfToken, VertexStream> m_Streams;

    struct StagingData
    {
        struct SourceData
        {
            std::shared_ptr<pxr::HdBufferSource> Source;
            RefCntAutoPtr<IBuffer>               Buffer;
        };

        std::unordered_map<pxr::TfToken, SourceData, pxr::TfToken::HashFunctor> Sources;

        std::shared_ptr<VertexData> ExistingData;
    };
    std::unique_ptr<StagingData> m_StagingData;
};

class HnGeometryPool::VertexHandleImpl final : public HnGeometryPool::VertexHandle
{
public:
    VertexHandleImpl(std::shared_ptr<VertexData> Data) :
        m_Data{std::move(Data)}
    {
    }

    virtual IBuffer* GetBuffer(const pxr::TfToken& Name) const override final
    {
        return m_Data ? m_Data->GetBuffer(Name) : nullptr;
    }

    virtual Uint32 GetNumVertices() const override final
    {
        return m_Data ? m_Data->GetNumVertices() : 0;
    }

    virtual Uint32 GetStartVertex() const override final
    {
        return m_Data ? m_Data->GetStartVertex() : 0;
    }

    std::shared_ptr<VertexData> GetData() const
    {
        return m_Data;
    }

    void SetData(std::shared_ptr<VertexData> Data)
    {
        m_Data = std::move(Data);
    }

private:
    std::shared_ptr<VertexData> m_Data;
};

class HnGeometryPool::IndexData final
{
public:
    IndexData(std::string            Name,
              pxr::VtValue           Indices,
              Uint32                 StartVertex,
              GLTF::ResourceManager* pResMgr) :
        m_Name{std::move(Name)},
        m_StagingData{std::make_unique<StagingData>(std::move(Indices), StartVertex)}
    {
        if (m_StagingData->Size == 0)
        {
            UNEXPECTED("No index data provided");
            return;
        }

        m_NumIndices = static_cast<Uint32>(m_StagingData->Size / sizeof(Uint32));
        if (pResMgr != nullptr && m_NumIndices > 0)
        {
            m_Suballocation = pResMgr->AllocateIndices(m_StagingData->Size);
        }
    }

    IBuffer* GetBuffer() const
    {
        return m_Buffer;
    }

    Uint32 GetStartIndex() const
    {
        return m_Suballocation ? m_Suballocation->GetOffset() / sizeof(Uint32) : 0;
    }

    Uint32 GetNumIndices() const
    {
        return m_NumIndices;
    }

    void Update(IRenderDevice*  pDevice,
                IDeviceContext* pContext)
    {
        if (!m_StagingData)
        {
            UNEXPECTED("No staging data. This may indicate the Update() method is called more than once, which is a bug.");
            return;
        }

        if (m_StagingData->Size == 0)
        {
            UNEXPECTED("No index data provided");
            return;
        }

        VERIFY(m_StagingData->Size / sizeof(Uint32) == m_NumIndices, "Unexpected number of indices in the staging data");

        BufferData IBData{m_StagingData->Ptr, m_StagingData->Size};
        if (m_Suballocation == nullptr)
        {
            if (m_Buffer)
            {
                pContext->UpdateBuffer(m_Buffer, 0, IBData.DataSize, IBData.pData, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            }
            else
            {
                BufferDesc Desc{
                    m_Name.c_str(),
                    IBData.DataSize,
                    BIND_INDEX_BUFFER,
                    USAGE_DEFAULT,
                };
                pDevice->CreateBuffer(Desc, &IBData, &m_Buffer);
                VERIFY_EXPR(m_Buffer != nullptr);
            }

            StateTransitionDesc Barrier{m_Buffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_INDEX_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE};
            pContext->TransitionResourceStates(1, &Barrier);
        }
        else
        {
            m_Buffer = m_Suballocation->GetBuffer();
            VERIFY_EXPR(m_Suballocation->GetSize() == IBData.DataSize);
            pContext->UpdateBuffer(m_Buffer, m_Suballocation->GetOffset(), IBData.DataSize, IBData.pData, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }

        m_StagingData.reset();
    }

private:
    const std::string m_Name;
    Uint32            m_NumIndices = 0;

    RefCntAutoPtr<IBuffer>              m_Buffer;
    RefCntAutoPtr<IBufferSuballocation> m_Suballocation;

    struct StagingData
    {
        pxr::VtValue Indices;

        const void* Ptr  = nullptr;
        size_t      Size = 0;

        StagingData(pxr::VtValue _Indices,
                    Uint32       StartVertex) :
            Indices{std::move(_Indices)}
        {
            if (Indices.IsHolding<pxr::VtVec3iArray>())
            {
                if (StartVertex != 0)
                {
                    pxr::VtVec3iArray IndicesArray = Indices.UncheckedRemove<pxr::VtVec3iArray>();
                    for (pxr::GfVec3i& idx : IndicesArray)
                    {
                        idx[0] += StartVertex;
                        idx[1] += StartVertex;
                        idx[2] += StartVertex;
                    }
                    Indices = pxr::VtValue::Take(IndicesArray);
                }
                const pxr::VtVec3iArray& IndicesArray = Indices.UncheckedGet<pxr::VtVec3iArray>();

                Ptr  = IndicesArray.data();
                Size = IndicesArray.size() * sizeof(pxr::GfVec3i);
            }
            else if (Indices.IsHolding<pxr::VtVec2iArray>())
            {
                if (StartVertex != 0)
                {
                    pxr::VtVec2iArray IndicesArray = Indices.UncheckedRemove<pxr::VtVec2iArray>();
                    for (pxr::GfVec2i& idx : IndicesArray)
                    {
                        idx[0] += StartVertex;
                        idx[1] += StartVertex;
                    }
                    Indices = pxr::VtValue::Take(IndicesArray);
                }

                const pxr::VtVec2iArray& IndicesArray = Indices.UncheckedGet<pxr::VtVec2iArray>();

                Ptr  = IndicesArray.data();
                Size = IndicesArray.size() * sizeof(pxr::GfVec2i);
            }
            else if (Indices.IsHolding<pxr::VtIntArray>())
            {
                if (StartVertex != 0)
                {
                    pxr::VtIntArray IndicesArray = Indices.UncheckedRemove<pxr::VtIntArray>();
                    for (int& idx : IndicesArray)
                        idx += StartVertex;
                    Indices = pxr::VtValue::Take(IndicesArray);
                }

                const pxr::VtIntArray& IndicesArray = Indices.UncheckedGet<pxr::VtIntArray>();

                Ptr  = IndicesArray.data();
                Size = IndicesArray.size() * sizeof(int);
            }
            else
            {
                UNEXPECTED("Unexpected index data type: ", Indices.GetTypeName());
            }
        }
    };
    std::unique_ptr<StagingData> m_StagingData;
};

class HnGeometryPool::IndexHandleImpl final : public HnGeometryPool::IndexHandle
{
public:
    IndexHandleImpl(std::shared_ptr<IndexData> Data) :
        m_Data{std::move(Data)}
    {
    }

    virtual IBuffer* GetBuffer() const override final
    {
        return m_Data ? m_Data->GetBuffer() : nullptr;
    }

    virtual Uint32 GetNumIndices() const override final
    {
        return m_Data ? m_Data->GetNumIndices() : 0;
    }

    virtual Uint32 GetStartIndex() const override final
    {
        return m_Data ? m_Data->GetStartIndex() : 0;
    }

    void SetData(std::shared_ptr<IndexData> Data)
    {
        m_Data = std::move(Data);
    }

private:
    std::shared_ptr<IndexData> m_Data;
};

void HnGeometryPool::AllocateVertices(const std::string&             Name,
                                      const BufferSourcesMapType&    Sources,
                                      std::shared_ptr<VertexHandle>& Handle)
{
    if (Sources.empty())
    {
        UNEXPECTED("No vertex data sources provided");
        return;
    }

    std::shared_ptr<VertexData> ExistingData;
    if (Handle)
    {
        ExistingData = static_cast<VertexHandleImpl*>(Handle.get())->GetData();
        if (!ExistingData->IsCompatible(Sources))
        {
            UNEXPECTED("Existing vertex data is not compatible with the provided sources. "
                       "This indicates that the number of vertices, the element size or the vertex data source names have changed. "
                       "In any of this cases the mesh should request a new vertex handle.");
            Handle.reset();
            ExistingData.reset();
        }
        if (ExistingData && ExistingData->GetStreamCount() == Sources.size())
        {
            // All streams in the existing data are present in the new sources - no data to move
            ExistingData.reset();
        }
    }

    VertexData::VertexStreamHashesType Hashes = VertexData::ComputeVertexStreamHashes(Sources, ExistingData.get());

    size_t Hash = 0;
    for (const auto& hash_it : Hashes)
    {
        Hash = pxr::TfHash::Combine(Hash, hash_it.second);
    }

    std::shared_ptr<VertexData> Data = m_VertexCache.Get(
        Hash,
        [&]() {
            std::shared_ptr<VertexData> Data = std::make_shared<VertexData>(Name, Sources, Hashes, m_UseVertexPool ? &m_ResMgr : nullptr, ExistingData);

            {
                std::lock_guard<std::mutex> Guard{m_PendingVertexDataMtx};
                m_PendingVertexData.emplace_back(Data);
            }

            return Data;
        });


    if (Handle)
    {
        static_cast<VertexHandleImpl*>(Handle.get())->SetData(std::move(Data));
    }
    else
    {
        Handle = std::make_shared<VertexHandleImpl>(std::move(Data));
    }
}

void HnGeometryPool::AllocateIndices(const std::string&                            Name,
                                     pxr::VtValue                                  Indices,
                                     Uint32                                        StartVertex,
                                     std::shared_ptr<HnGeometryPool::IndexHandle>& Handle)
{
    if (Indices.IsEmpty())
    {
        UNEXPECTED("No index data provided");
        return;
    }

    size_t Hash = Indices.GetHash();
    Hash        = pxr::TfHash::Combine(Hash, StartVertex);

    std::shared_ptr<IndexData> Data = m_IndexCache.Get(
        Hash,
        [&]() {
            std::shared_ptr<IndexData> Data = std::make_shared<IndexData>(Name, std::move(Indices), StartVertex, m_UseIndexPool ? &m_ResMgr : nullptr);

            {
                std::lock_guard<std::mutex> Guard{m_PendingIndexDataMtx};
                m_PendingIndexData.emplace_back(Data);
            }

            return Data;
        });

    if (Handle)
    {
        static_cast<IndexHandleImpl*>(Handle.get())->SetData(std::move(Data));
    }
    else
    {
        Handle = std::make_shared<IndexHandleImpl>(std::move(Data));
    }
}


void HnGeometryPool::Commit(IDeviceContext* pContext)
{
    {
        std::lock_guard<std::mutex> Guard{m_PendingVertexDataMtx};
        for (auto& Data : m_PendingVertexData)
        {
            Data->Update(m_pDevice, pContext);
        }
        m_PendingVertexData.clear();
    }

    {
        std::lock_guard<std::mutex> Guard{m_PendingIndexDataMtx};
        for (auto& Data : m_PendingIndexData)
        {
            Data->Update(m_pDevice, pContext);
        }
        m_PendingIndexData.clear();
    }
}

} // namespace USD

} // namespace Diligent
