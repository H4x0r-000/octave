#include "Nodes/3D/Node3d.h"

#include "AssetManager.h"
#include "Nodes/Node.h"
#include "World.h"
#include "Renderer.h"
#include "Maths.h"
#include "Assets/SkeletalMesh.h"

#include "Nodes/3D/SkeletalMesh3d.h"

FORCE_LINK_DEF(Node3D);
DEFINE_NODE(Node3D);

bool HandleTransformPropChange(Datum* datum, uint32_t index, const void* newValue)
{
    Property* prop = static_cast<Property*>(datum);
    OCT_ASSERT(prop != nullptr);
    Node3D* transformComp = static_cast<Node3D*>(prop->mOwner);
    bool success = false;

    if (prop->mName == "Rotation")
    {
        transformComp->SetRotation(*((glm::vec3*)(newValue)));
        success = true;
    }
    else if (prop->mName == "Position")
    {
        transformComp->SetPosition(*((glm::vec3*)(newValue)));
        success = true;
    }
    else if (prop->mName == "Scale")
    {
        transformComp->SetScale(*((glm::vec3*)(newValue)));
        success = true;
    }

    transformComp->MarkTransformDirty();

    return success;
}

bool Node3D::OnRep_RootPosition(Datum* datum, uint32_t index, const void* newValue)
{
    Actor* actor = (Actor*)datum->mOwner;
    OCT_ASSERT(actor != nullptr);
    glm::vec3* newPos = (glm::vec3*) newValue;
    actor->GetRootComponent()->SetPosition(*newPos);
    return true;
}

bool Node3D::OnRep_RootRotation(Datum* datum, uint32_t index, const void* newValue)
{
    Actor* actor = (Actor*)datum->mOwner;
    OCT_ASSERT(actor != nullptr);

    glm::vec3* newRot = (glm::vec3*) newValue;
    actor->GetRootComponent()->SetRotation(*newRot);

    return true;
}

bool Node3D::OnRep_RootScale(Datum* datum, uint32_t index, const void* newValue)
{
    Actor* actor = (Actor*)datum->mOwner;
    OCT_ASSERT(actor != nullptr);
    glm::vec3* newScale = (glm::vec3*) newValue;
    actor->GetRootComponent()->SetScale(*newScale);
    return true;
}

Node3D::Node3D() :
    mParent(nullptr),
    mPosition(0,0,0),
    mRotationEuler(0,0,0),
    mScale(1,1,1),
    mRotationQuat({0, 0, 0}),
    mTransform(1.0f),
    mParentBoneIndex(-1),
    mTransformDirty(true)
{
    mName = "Transform";
}

Node3D::~Node3D()
{

}

void Node3D::SaveStream(Stream& stream)
{
    Component::SaveStream(stream);
    stream.WriteVec3(mPosition);
    stream.WriteQuat(mRotationQuat);
    stream.WriteVec3(mScale);
}

void Node3D::LoadStream(Stream& stream)
{
    Component::LoadStream(stream);
    mPosition = stream.ReadVec3();
    mRotationQuat = stream.ReadQuat();
    mScale = stream.ReadVec3();
}

void Node3D::Create()
{
    Component::Create();
}

void Node3D::Destroy()
{
    Component::Destroy();

    if (GetWorld() &&
        GetWorld()->GetAudioReceiver() == this)
    {
        GetWorld()->SetAudioReceiver(nullptr);
    }

#if DEBUG_DRAW_ENABLED
    Renderer::Get()->RemoveDebugDrawsForNode(this);
#endif
}

void Node3D::Tick(float deltaTime)
{

}

const char* Node3D::GetTypeName() const
{
    return "Transform";
}

void Node3D::GatherProperties(std::vector<Property>& outProps)
{
    Node::GatherProperties(outProps);

    outProps.push_back(Property(DatumType::Vector, "Position", this, &mPosition, 1, HandleTransformPropChange));
    outProps.push_back(Property(DatumType::Vector, "Rotation", this, &mRotationEuler, 1, HandleTransformPropChange));
    outProps.push_back(Property(DatumType::Vector, "Scale", this, &mScale, 1, HandleTransformPropChange));
}

void Node3D::GatherReplicatedData(std::vector<NetDatum>& outData)
{
    Node::GatherReplicatedData(outData);

    if (mReplicateTransform)
    {
        outData.push_back(NetDatum(DatumType::Vector, this, &mPosition, 1, OnRep_RootPosition));
        outData.push_back(NetDatum(DatumType::Vector, this, &mRotationEuler, 1, OnRep_RootRotation));
        outData.push_back(NetDatum(DatumType::Vector, this, &mScale, 1, OnRep_RootScale));
    }
}

bool Node3D::IsTransformNode() const
{
    return true;
}

void Node3D::AttachToBone(SkeletalMesh3D* parent, const char* boneName, bool keepWorldTransform)
{
    int32_t parentBoneIndex = parent->FindBoneIndex(boneName);
    AttachToBone(parent, parentBoneIndex, keepWorldTransform);
}

void Node3D::AttachToBone(SkeletalMesh3D* parent, int32_t boneIndex, bool keepWorldTransform)
{
    glm::mat4 origWorldTransform;
    if (keepWorldTransform)
    {
        origWorldTransform = GetTransform();
    }

    Attach(parent);
    mParentBoneIndex = boneIndex;

    if (keepWorldTransform)
    {
        SetTransform(origWorldTransform);
    }
}

void Node3D::MarkTransformDirty()
{
    // TODO-NODE: Consider propogating this to children nodes. 
    // It looks like Godot does it this way, and might remove some one-frame-delay bugs.
    mTransformDirty = true;
}

bool Node3D::IsTransformDirty() const
{
    return mTransformDirty;
}

void Node3D::UpdateTransform(bool updateChildren)
{
    // First we need to update parent transform if it's dirty.
    if (mParent != nullptr &&
        mParent->mTransformDirty)
    {
        mParent->UpdateTransform(false);
    }

    if (mTransformDirty)
    {
        // Update transform
        mTransform = glm::mat4(1);

        // Force uniform scale if the component has children.
        // Non-uniform scale was causing problems for children components because shear was 
        // getting introduced into the child transforms if the parent had any rotation.
        // Relevant Github issues:
        // https://github.com/BabylonJS/Babylon.js/issues/10579
        // https://github.com/mrdoob/three.js/issues/3845
        // https://github.com/armory3d/armory/issues/2211
        glm::vec3 scale = mScale;
        if (GetNumChildren() > 0)
        {
            scale = glm::vec3(mScale.x, mScale.x, mScale.x);
        }
        
        mTransform = glm::translate(mTransform, mPosition);
        mTransform *= glm::toMat4(mRotationQuat);
        mTransform = glm::scale(mTransform, scale);

        if (mParent != nullptr)
        {
            // Concatenate parent transform with this transform
            mTransform = GetParentTransform() * mTransform;
        }

        // Recursively mark children dirty since their parent has updated.
        for (uint32_t i = 0; i < mChildren.size(); ++i)
        {
            mChildren[i]->MarkTransformDirty();
        }

        // Cache off the euler angle rotation.
        mRotationEuler = GetRotationEuler();

        mTransformDirty = false;
    }

    // Recursively update child transforms.
    if (updateChildren)
    {
        for (uint32_t i = 0; i < mChildren.size(); ++i)
        {
            mChildren[i]->UpdateTransform(updateChildren);
        }
    }
}

void Node3D::GatherProxyDraws(std::vector<DebugDraw>& inoutDraws)
{
#if DEBUG_DRAW_ENABLED
    if (GetType() == Node3D::GetStaticType())
    {
        DebugDraw debugDraw;
        debugDraw.mMesh = LoadAsset<StaticMesh>("SM_Cube");
        debugDraw.mNode = this;
        debugDraw.mColor = glm::vec4(1.0f, 0.25f, 0.25f, 1.0f);
        debugDraw.mTransform = glm::scale(GetTransform(), { 0.2f, 0.2f, 0.2f });
        inoutDraws.push_back(debugDraw);
    }
#endif
}

bool Node3D::IsTransient() const
{
    bool transient = mTransient;

    if (!transient &&
        mParent != nullptr)
    {
        transient = mParent->IsTransient();
    }

    return transient;
}

glm::vec3 Node3D::GetPosition() const
{
    return mPosition;
}

glm::vec3 Node3D::GetRotationEuler() const
{
    glm::vec3 eulerAngles = glm::eulerAngles(mRotationQuat) * RADIANS_TO_DEGREES;

    eulerAngles = EnforceEulerRange(eulerAngles);

    return eulerAngles;
}

glm::quat Node3D::GetRotationQuat() const
{
    return mRotationQuat;
}

glm::vec3 Node3D::GetScale() const
{
    return mScale;
}

glm::vec3& Node3D::GetPositionRef()
{
    return mPosition;
}

glm::vec3& Node3D::GetRotationEulerRef()
{
    return mRotationEuler;
}

glm::quat& Node3D::GetRotationQuatRef()
{
    return mRotationQuat;
}

glm::vec3& Node3D::GetScaleRef()
{
    return mScale;
}

const glm::mat4& Node3D::GetTransform()
{
    // TODO-NODE: I added this update transform check and made this method non-const.
    // Is this causing any bugs? Performance issues?
    if (mTransformDirty)
    {
        UpdateTransform(false);
    }

    return mTransform;
}

void Node3D::SetPosition(glm::vec3 position)
{
    mPosition = position;
    MarkTransformDirty();
}

void Node3D::SetRotation(glm::vec3 rotation)
{
    SetRotation(glm::quat(rotation * DEGREES_TO_RADIANS));
}

void Node3D::SetRotation(glm::quat quat)
{
    mRotationQuat = glm::normalize(quat);
    MarkTransformDirty();
}

void Node3D::SetScale(glm::vec3 scale)
{
    mScale = scale;
    MarkTransformDirty();
}

void Node3D::SetTransform(const glm::mat4& transform)
{
    mTransform = transform;

    // Update the relative transforms to match the new absolute transform.
    SetAbsolutePosition(Maths::ExtractPosition(transform));
    SetAbsoluteScale(Maths::ExtractScale(transform));
    SetAbsoluteRotation(Maths::ExtractRotation(transform));
    mRotationEuler = GetRotationEuler();

    mTransformDirty = false;

    for (uint32_t i = 0; i < mChildren.size(); ++i)
    {
        //mChildren[i]->UpdateTransform();
        mChildren[i]->MarkTransformDirty();
    }
}

glm::vec3 Node3D::GetAbsolutePosition()
{
    UpdateTransform(false);
    return Maths::ExtractPosition(mTransform);
}

glm::vec3 Node3D::GetAbsoluteRotationEuler()
{
    UpdateTransform(false);

    glm::vec3 eulerAngles = glm::eulerAngles(Maths::ExtractRotation(mTransform)) * RADIANS_TO_DEGREES;

    eulerAngles = EnforceEulerRange(eulerAngles);

    return eulerAngles;
}

glm::quat Node3D::GetAbsoluteRotationQuat()
{
    UpdateTransform(false);
    return Maths::ExtractRotation(mTransform);
}

glm::vec3 Node3D::GetAbsoluteScale()
{
    UpdateTransform(false);
    return Maths::ExtractScale(mTransform);
}

void Node3D::SetAbsolutePosition(glm::vec3 position)
{
    if (mParent != nullptr)
    {
        glm::mat4 invParentTrans = glm::inverse(GetParentTransform());
        glm::vec4 position4 = glm::vec4(position, 1.0f);
        glm::vec4 relPosition4 = invParentTrans * position4;
        SetPosition(glm::vec3(relPosition4.x, relPosition4.y, relPosition4.z));
    }
    else
    {
        SetPosition(position);
    }
}

void Node3D::SetAbsoluteRotation(glm::vec3 rotation)
{
    glm::quat quat = glm::quat(rotation * DEGREES_TO_RADIANS);
    SetAbsoluteRotation(quat);
}

void Node3D::SetAbsoluteRotation(glm::quat rotation)
{
    glm::quat newRelativeRot = mRotationQuat;

    // Convert the world rotation to relative rotation
    if (mParent != nullptr)
    {
        glm::quat parentWorldRot = mParent->GetAbsoluteRotationQuat();

        if (mParentBoneIndex != -1 &&
            mParent->GetType() == SkeletalMesh3D::GetStaticType())
        {
            SkeletalMesh3D* skComp = (SkeletalMesh3D*) mParent;
            parentWorldRot = parentWorldRot * skComp->GetBoneRotationQuat(mParentBoneIndex);
        }

        newRelativeRot = glm::inverse(parentWorldRot) * rotation;
    }
    else
    {
        // If no parent, then the world rotation is the relative rotation.
        newRelativeRot = rotation;
    }

    SetRotation(newRelativeRot);
}

void Node3D::SetAbsoluteScale(glm::vec3 scale)
{
    if (mParent != nullptr)
    {
#if 0
        // Old code that was causing some problems.
        glm::mat4 invParentTrans = glm::inverse(GetParentTransform());
        glm::vec4 scale4 = glm::vec4(scale, 0.0f);
        glm::vec4 relScale4 = invParentTrans * scale4;
        SetScale(glm::vec3(relScale4.x, relScale4.y, relScale4.z));
#else
        glm::vec3 parentScale = mParent->GetAbsoluteScale();
        glm::vec3 relScale;
        relScale.x = (parentScale.x != 0.0f) ? scale.x / parentScale.x : 0.0f;
        relScale.y = (parentScale.y != 0.0f) ? scale.y / parentScale.y : 0.0f;
        relScale.z = (parentScale.z != 0.0f) ? scale.z / parentScale.z : 0.0f;
        SetScale(relScale);
#endif
    }
    else
    {
        SetScale(scale);
    }
}

void Node3D::AddRotation(glm::quat rotation)
{
    SetRotation(rotation * mRotationQuat);
}

void Node3D::AddRotation(glm::vec3 rotation)
{
    glm::quat rotQuat = glm::quat(rotation * DEGREES_TO_RADIANS);
    AddRotation(rotQuat);
    //SetRotation(GetRotationEuler() + rotation);
}

void Node3D::AddAbsoluteRotation(glm::quat rotation)
{
    // Get component's world rotation first
    glm::quat newWorldRot = GetAbsoluteRotationQuat();

    // Add the world rotation to the component's world rotation (the new world rotation)
    newWorldRot = rotation * newWorldRot;

    SetAbsoluteRotation(newWorldRot);
}

void Node3D::AddAbsoluteRotation(glm::vec3 rotation)
{
    glm::quat rotQuat = glm::quat(rotation);
    AddAbsoluteRotation(rotQuat);
}

void Node3D::RotateAround(glm::vec3 pivot, glm::vec3 axis, float degrees)
{
    // Work in world space
    UpdateTransform(false);

    glm::mat4 trans = mTransform;
    trans = glm::translate(trans, pivot);
    trans = glm::rotate(trans, degrees * DEGREES_TO_RADIANS, axis);
    trans = glm::translate(trans, -pivot);

    SetTransform(trans);
}

void Node3D::LookAt(glm::vec3 target, glm::vec3 up)
{
    glm::mat4 rotMat = glm::lookAt(GetAbsolutePosition(), target, up);
    glm::quat rotQuat = glm::conjugate(glm::toQuat(rotMat));
    SetAbsoluteRotation(rotQuat);
}

glm::vec3 Node3D::GetCachedEulerRotation() const
{
    return mRotationEuler;
}

glm::vec3 Node3D::GetForwardVector() const
{
     glm::vec3 forwardVector = mTransform * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
     forwardVector = Maths::SafeNormalize(forwardVector);
    return forwardVector;
}

glm::vec3 Node3D::GetRightVector() const
{
    glm::vec3 rightVector = mTransform * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    rightVector = Maths::SafeNormalize(rightVector);
    return rightVector;
}

glm::vec3 Node3D::GetUpVector() const
{
    glm::vec3 upVector = mTransform * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
    upVector = Maths::SafeNormalize(upVector);
    return upVector;
}

glm::mat4 Node3D::GetParentTransform()
{
    glm::mat4 transform(1);

    if (mParent != nullptr)
    {
        if (mParentBoneIndex == -1)
        {
            transform = mParent->GetTransform();
        }
        else if (mParent->GetType() == SkeletalMesh3D::GetStaticType())
        {
            SkeletalMesh3D* skComp = (SkeletalMesh3D*)mParent;

            if (mParentBoneIndex >= 0 &&
                mParentBoneIndex < int32_t(skComp->GetNumBones()) &&
                skComp->GetSkeletalMesh() != nullptr)
            {
                transform = mParent->GetTransform() *
                    skComp->GetBoneTransform(mParentBoneIndex) *
                    skComp->GetSkeletalMesh()->GetBone(mParentBoneIndex).mInvOffsetMatrix;
            }
        }
    }

    return transform;
}

int32_t Node3D::GetParentBoneIndex() const
{
    return mParentBoneIndex;
}

void Node3D::Attach(Node* parent, bool keepWorldTransform)
{
    // Can't attach to self.
    OCT_ASSERT(parent != this);
    if (parent == this)
    {
        return;
    }

    if (keepWorldTransform && IsTransformDirty())
    {
        UpdateTransform(false);
    }

    // Detach from parent first
    if (mParent != nullptr)
    {
        if (keepWorldTransform)
        {
            glm::mat4 transform = GetTransform();
            mParent->RemoveChild(this);
            SetTransform(transform);
        }
        else
        {
            mParent->RemoveChild(this);
        }
    }

    mParentBoneIndex = -1;

    // Attach to new parent
    if (parent != nullptr)
    {
        if (keepWorldTransform)
        {
            glm::mat4 transform = GetTransform();
            parent->AddChild(this);
            SetTransform(transform);
        }
        else
        {
            parent->AddChild(this);
        }
    }

    MarkTransformDirty();
}
