#include "Nodes/Node.h"
#include "Log.h"
#include "World.h"
#include "Renderer.h"
#include "Clock.h"
#include "Enums.h"
#include "Maths.h"
#include "Utilities.h"
#include "Engine.h"
#include "ObjectRef.h"
#include "NetworkManager.h"
#include "LuaBindings/Actor_Lua.h"
#include "Assets/Blueprint.h"

#include "Nodes/3D/TransformComponent.h"
#include "Nodes/3D/StaticMeshComponent.h"
#include "Nodes/3D/SkeletalMeshComponent.h"
#include "Nodes/3D/PointLightComponent.h"
#include "Nodes/3D/DirectionalLightComponent.h"
#include "Nodes/3D/CameraComponent.h"
#include "Nodes/3D/BoxComponent.h"
#include "Nodes/3D/SphereComponent.h"
#include "Nodes/3D/ParticleComponent.h"
#include "Nodes/3D/AudioComponent.h"
#include "Nodes/3D/PrimitiveComponent.h"
#include "Nodes/3D/LightComponent.h"

#include "Graphics/Graphics.h"

#if EDITOR
#include "EditorState.h"
#endif

#include <functional>
#include <algorithm>

#define INVOKE_NET_FUNC_BODY(P) \
    NetFunc* netFunc = FindNetFunc(name); \
    OCT_ASSERT(netFunc->mNumParams == P); \
    bool shouldExecute = ShouldExecuteNetFunc(netFunc->mType, this); \
    SendNetFunc(netFunc, P, params);

std::unordered_map<TypeId, NetFuncMap> Node::sTypeNetFuncMap;

#define ENABLE_SCRIPT_FUNCS 1

DEFINE_SCRIPT_LINK_BASE(Node);

FORCE_LINK_DEF(Node);
DEFINE_FACTORY_MANAGER(Node);
DEFINE_FACTORY(Node, Node);
DEFINE_RTTI(Node);

Node::Node()
{
    mName = "Node";
}

Node::~Node()
{

}

void Node::Create()
{
    REGISTER_SCRIPT_FUNCS();
}

void Node::Destroy()
{
    // Destroy+Stop children first. Maybe we need to split up Stop + Destroy()?
    // Could call RecursiveStop() before destroying everything?
    for (int32_t i = int32_t(GetNumChildren()) - 1; i >= 0; --i)
    {
        Node* child = GetChild(i);
        child->Destroy();
        delete child;
    }

    if (mHasStarted)
    {
        Stop();
    }

    if (IsPrimitiveNode() && GetWorld())
    {
        GetWorld()->PurgeOverlaps(static_cast<PrimitiveComponent*>(this));
    }

    if (mParent != nullptr)
    {
        Attach(nullptr);
    }

    NodeRef::EraseReferencesToObject(this);

#if EDITOR
    GetWorld()->DeselectComponent(this);
#endif
}

void Node::SaveStream(Stream& stream)
{
    // TODO-NODE: Can we just entirely remove Save/LoadStream from Nodes 
    // and just serialize the properties? Could simplify things. Or instead of
    // totally removing Save/LoadStream(), still allow nodes to override it
    // but just delete all of the stuff that could be serialized by properties.

    stream.WriteString(mName);
    stream.WriteBool(mActive);
    stream.WriteBool(mVisible);

    // Tags
    OCT_ASSERT(mTags.size() <= 255);
    uint32_t numTags = glm::min((uint32_t)mTags.size(), 255u);
    stream.WriteUint8(numTags);

    for (uint32_t i = 0; i < numTags; ++i)
    {
        stream.WriteString(mTags[i]);
    }

    stream.WriteBool(mReplicate);
    stream.WriteBool(mReplicateTransform);

    // TODO-NODE: Script serailization? Possibly just serialize-by-properties.
}

void Node::LoadStream(Stream& stream)
{
    // TODO-NODE: Remove old data loading after serializing everything.
#if 1
    // Load old data
    stream.ReadString(mName);
    mActive = stream.ReadBool();
    mVisible = stream.ReadBool();
#else
    // Load new data
    stream.ReadString(mName);
    mActive = stream.ReadBool();
    mVisible = stream.ReadBool();


    uint32_t numTags = (uint32_t)stream.ReadUint8();
    mTags.resize(numTags);
    for (uint32_t i = 0; i < numTags; ++i)
    {
        stream.ReadString(mTags[i]);
    }

    mReplicate = stream.ReadBool();
    mReplicateTransform = stream.ReadBool();

    // TODO-NODE: Script serailization? Possibly just serialize-by-properties.
#endif
}

void Node::Copy(Node* srcNode)
{
    OCT_ASSERT(srcNode);
    OCT_ASSERT(srcNode->GetType() == GetType());

    if (srcNode == nullptr ||
        srcNode->GetType() != GetType())
    {
        LogError("Failed to copy node");
        return;
    }

    // I'm not using CopyPropertyValues() here because we need to handle the special
    // case where "Filename" is copied. Should refactor this code so we can use the CopyPropertyValues() func.
    // Possibly, just copy over ScriptProperties separately after initial pass.
    std::vector<Property> srcProps;
    srcNode->GatherProperties(srcProps);

    std::vector<Property> dstProps;
    GatherProperties(dstProps);

    for (uint32_t i = 0; i < srcProps.size(); ++i)
    {
        Property* srcProp = &srcProps[i];
        Property* dstProp = nullptr;

        for (uint32_t j = 0; j < dstProps.size(); ++j)
        {
            if (dstProps[j].mName == srcProp->mName &&
                dstProps[j].mType == srcProp->mType)
            {
                dstProp = &dstProps[j];
            }
        }

        if (dstProp != nullptr)
        {
            if (dstProp->IsVector())
            {
                dstProp->ResizeVector(srcProp->GetCount());
            }
            else
            {
                OCT_ASSERT(dstProp->mCount == srcProp->mCount);
            }

            dstProp->SetValue(srcProp->mData.vp, 0, srcProp->mCount);
        }

        // TODO-NODE: Gather properties if this uses a script.
        // For script components... if we first copy over the Filename property,
        // that will change the number of properties on the script so we need to regather them.
        // Script component is really the only component that can dynamically change its properties,
        // so I'm adding a hack now just for script component.
        if (srcProp->mName == "Filename")
        {
            dstProps.clear();
            GatherProperties(dstProps);
        }
    }

    mSceneSource = srcNode->GetSceneSource();

    // Copy children recursively.
    for (uint32_t i = 0; i < srcNode->GetNumChildren(); ++i)
    {
        Node* srcChild = srcNode->GetChild(i);
        Node* dstChild = nullptr;

        if (i >= GetNumChildren())
        {
            dstChild = CreateComponent(srcChild->GetType());
        }
        else
        {
            dstChild = GetChild(i);
        }

        dstChild->Copy(srcChild);
    }
}

void Node::Render(PipelineId pipelineId)
{
    // TODO-NODE: Need to implement RecursiveRender(). Replace Widget's RecursiveRender()?
    // TODO-NODE: This function is used when rendering hit check and selected geometry I believe.
    // Could probably adjust Render() function in Primitive3D + Widget so that it can take a pipeline.
    // Or just manually bind the pipeline from the callers.
    if (IsPrimitiveNode() && IsVisible())
    {
        PrimitiveComponent* primComp = static_cast<PrimitiveComponent*>(this);
        GFX_BindPipeline(pipelineId, primComp->GetVertexType());
        primComp->Render();
    }
}

void Node::Start()
{

}

void Node::Stop()
{

}

void Node::Tick(float deltaTime)
{
    // TODO-NODE: Need to implement RecursiveTick(). Replace Widget's RecursiveUpdate()?
}

void Node::EditorTick(float deltaTime)
{
    Tick(deltaTime);
}

void Node::GatherProperties(std::vector<Property>& outProps)
{
    outProps.push_back({DatumType::String, "Name", this, &mName});
    outProps.push_back({DatumType::Bool, "Active", this, &mActive});
    outProps.push_back({DatumType::Bool, "Visible", this, &mVisible});

    outProps.push_back(Property(DatumType::Bool, "Persistent", this, &mPersistent));
    outProps.push_back(Property(DatumType::Bool, "Replicate", this, &mReplicate));
    outProps.push_back(Property(DatumType::Bool, "Replicate Transform", this, &mReplicateTransform));
    outProps.push_back(Property(DatumType::String, "Tags", this, &mTags).MakeVector());
}

void Node::GatherReplicatedData(std::vector<NetDatum>& outData)
{
    outData.push_back(NetDatum(DatumType::Byte, this, &mOwningHost));
}

void Node::GatherNetFuncs(std::vector<NetFunc>& outFuncs)
{

}

// TODO-NODE: Register / unregister should happen on AddChild() / SetParent()
// If the prev world != newWorld, set its world and also call register/unregister.
void Component::SetOwner(Actor* owner)
{
    World* prevWorld = mOwner ? mOwner->GetWorld() : nullptr;
    mOwner = owner;
    World* newWorld = mOwner ? mOwner->GetWorld() : nullptr;

    if (prevWorld != newWorld)
    {
        if (prevWorld != nullptr)
        {
            prevWorld->UnregisterComponent(this);
        }

        if (newWorld != nullptr)
        {
            newWorld->RegisterComponent(this);
        }
    }
}

void Node::SetName(const std::string& newName)
{
    mName = newName;
}

const std::string& Node::GetName() const
{
    return mName;
}

void Node::SetActive(bool active)
{
    mActive = active;
}

bool Node::IsActive() const
{
    return mActive;
}

void Node::SetVisible(bool visible)
{
    mVisible = visible;
}

bool Node::IsVisible() const
{
    return mVisible;
}

void Node::SetTransient(bool transient)
{
    mTransient = transient;
}

bool Node::IsTransient() const
{
    return mTransient;
}

void Node::SetDefault(bool isDefault)
{
    mDefault = isDefault;
}

bool Node::IsDefault() const
{
    return mDefault;
}

World* Node::GetWorld()
{
    return mWorld;
}

const char* Node::GetTypeName() const
{
    return "Node";
}

DrawData Node::GetDrawData()
{
    DrawData ret = {};
    ret.mComponent = nullptr;
    ret.mMaterial = nullptr;
    return ret;
}

bool Node::IsTransformNode() const
{
    return false;
}

bool Node::IsPrimitiveNode() const
{
    return false;
}

bool Node::IsLightNode() const
{
    return false;
}

Node * Node::GetParent()
{
    return mParent;
}

const std::vector<Node*>& Node::GetChildren() const
{
    return mChildren;
}

void Node::Attach(Node* parent, bool keepWorldTransform)
{
    // Can't attach to self.
    OCT_ASSERT(parent != this);
    if (parent == this)
    {
        return;
    }

    // Detach from parent first
    if (mParent != nullptr)
    {
        mParent->RemoveChild(this);
    }

    // Attach to new parent
    if (parent != nullptr)
    {
        parent->AddChild(this);
    }
}

void Node::AddChild(Node* child)
{
    if (child != nullptr)
    {
        // Check to make sure we aren't adding a duplicate
        bool childFound = false;
        for (uint32_t i = 0; i < mChildren.size(); ++i)
        {
            if (mChildren[i] == child)
            {
                childFound = true;
                break;
            }
        }

        OCT_ASSERT(!childFound); // Child already parented to this node?
        if (!childFound)
        {
            mChildren.push_back(child);
            child->mParent = this;
        }
    }
}

void Node::RemoveChild(Node* child)
{
    if (child != nullptr)
    {
        int32_t childIndex = -1;
        for (int32_t i = 0; i < int32_t(mChildren.size()); ++i)
        {
            if (mChildren[i] == child)
            {
                childIndex = i;
                break;
            }
        }

        OCT_ASSERT(childIndex != -1); // Could not find the component to remove
        if (childIndex != -1)
        {
            RemoveChild(childIndex);
        }
    }
}

void Node::RemoveChild(int32_t index)
{
    OCT_ASSERT(index >= 0 && index < int32_t(mChildren.size()));
    mChildren[index]->mParent = nullptr;
    mChildren.erase(mChildren.begin() + index);
}

int32_t Node::GetChildIndex(const char* childName)
{
    int32_t index = -1;
    for (int32_t i = 0; i < int32_t(mChildren.size()); ++i)
    {
        if (mChildren[i]->GetName() == childName)
        {
            index = i;
            break;
        }
    }

    return index;
}

Node* Node::GetChild(const char* childName)
{
    Node* retNode = nullptr;
    int32_t index = GetChildIndex(childName);
    if (index != -1)
    {
        retNode = GetChild(index);
    }
    return retNode;
}

Node* Node::GetChild(int32_t index)
{
    Node* retNode = nullptr;
    if (index >= 0 &&
        index < (int32_t)mChildren.size())
    {
        retNode = mChildren[index];
    }
    return retNode;
}

uint32_t Node::GetNumChildren() const
{
    return (uint32_t)mChildren.size();
}

int32_t Node::FindParentNodeIndex() const
{
    int32_t retIndex = -1;

    if (mParent != nullptr)
    {
        const std::vector<Node*>& children = mParent->GetChildren();
        for (uint32_t i = 0; i < children.size(); ++i)
        {
            if (children[i] == mParent)
            {
                retIndex = i;
                break;
            }
        }
    }

    return retIndex;
}
