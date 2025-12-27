#include <arcore/service.h>
#include <data/dataset.h>

namespace oc {

    ARCoreService::ARCoreService(void *env, void *context, Mode mode, bool flashlight) {
        renderer = new GLRenderer();
        google = nullptr;
        huawei = nullptr;
        mode_ = mode;

        if (mode >= HUAWEI_SFM)
            huawei = new AREngine(env, context, mode == Mode::HUAWEI_TOF, mode == Mode::HUAWEI_FACE, flashlight);
        else
            google = new ARCore(env, context, mode == Mode::GOOGLE_FACE, mode == Mode::GOOGLE_TOF);
    }

    ARCoreService::~ARCoreService() {
        if (mode_ >= HUAWEI_SFM)
            delete huawei;
        else
            delete google;
        delete renderer;
    }

    void ARCoreService::Clear(bool detach) {
        if (mode_ >= HUAWEI_SFM) {
            if (huawei) huawei->Clear(detach);
        } else {
            if (google) google->Clear(detach);
        }
        last_diff = -1;
    }

    void ARCoreService::OnPause() {
        if (mode_ >= HUAWEI_SFM) {
            if (huawei) huawei->OnPause();
        } else {
            if (google) google->OnPause();
        }
    }

    void ARCoreService::OnResume() {
        if (mode_ >= HUAWEI_SFM) {
            if (huawei) huawei->OnResume();
        } else {
            if (google) google->OnResume();
        }
    }

    void ARCoreService::OnDisplayGeometryChanged(int display_rotation, int width, int height, bool fullhd) {
        if (mode_ >= HUAWEI_SFM) {
            if (huawei) huawei->OnDisplayGeometryChanged(display_rotation, width, height);
        } else {
            if (google) google->OnDisplayGeometryChanged(display_rotation, width, height);
        }

        glViewport(0, 0, width, height);
        int w = 360;
        int h = 640;
        if (fullhd) {
            w = 1080;
            h = 1920;
        }
        renderer->Init(width, height, w, h);
    }

    void ARCoreService::Configure(void *session, void *frame) {
        if (mode_ >= HUAWEI_SFM) {
            if (huawei) huawei->Configure(static_cast<HwArSession *>(session), static_cast<HwArFrame *>(frame));
        } else {
            if (google) google->Configure(static_cast<ArSession *>(session), static_cast<ArFrame *>(frame));
        }
    }

    float ARCoreService::CountFrameError() {
        if (mode_ >= HUAWEI_SFM) {
            if (huawei) return huawei->CountFrameError();
            return 0.0f;
        } else {
            if (google) return google->CountFrameError();
            return 0.0f;
        }
    }

    bool ARCoreService::Process(bool update) {
        bool output;
        if (mode_ >= HUAWEI_SFM) {
            output = huawei ? huawei->Process(update) : false;
        } else {
            output = google ? google->Process(update) : false;
        }

        if (output) {
            glm::mat4 matrix = GetPose()[COLOR_CAMERA];
            glm::vec3 pos = glm::vec3(matrix[3][0], matrix[3][1], matrix[3][2]);
            glm::quat rot = glm::quat_cast(matrix);

            float value = oc::GLCamera::Diff(pos, image_position, rot, image_rotation);
            if (last_diff >= 0) {
                last_diff = value > last_diff ? value : 0.95f * last_diff + 0.05f * value;
            } else {
                last_diff = value;
            }
            image_position = pos;
            image_rotation = rot;
        }
        return output;
    }


    std::vector<glm::vec3> ARCoreService::GetActiveAnchors() {
        if (mode_ >= HUAWEI_SFM) {
            if (huawei) return huawei->GetActiveAnchors();
            return {};
        } else {
            if (google) return google->GetActiveAnchors();
            return {};
        }
    }

    std::vector<float> ARCoreService::GetDistortion() {
        if (mode_ >= HUAWEI_SFM) {
            if (huawei) return huawei->GetDistortion();
            return {};
        } else {
            if (google) return google->GetDistortion();
            return {};
        }
    }

    Mesh ARCoreService::GetFace() {
        if (mode_ >= HUAWEI_SFM) {
            if (huawei) return huawei->GetFace(GetProjection());
            return Mesh();
        } else {
            if (google) return google->GetFace(GetProjection() * glm::inverse(GetPose()[OPENGL_CAMERA]));
            return Mesh();
        }
    }

    Image *ARCoreService::GetImage(ARCoreCamera::Effect effect) {
        int w = renderer->rWidth;
        int h = renderer->rHeight;
        if ((effect == ARCoreCamera::Effect::DEPTH) || (effect == ARCoreCamera::Effect::EDGES)) {
            renderer->rWidth = 360;
            renderer->rHeight = 640;
        }

        renderer->Rtt(true);
        RenderCamera(effect);
        renderer->Rtt(false);
        Image* output = renderer->ReadRtt();

        renderer->rWidth = w;
        renderer->rHeight = h;
        return output;
    }

    ARCoreService::Mode ARCoreService::GetMode() {
        return mode_;
    }

    std::vector<glm::vec4> ARCoreService::GetPointCloud(float maxDiff) {
        std::vector<glm::vec4> output;
        bool validFrame = !GetActiveAnchors().empty() || IsFaceMode();
        if (!validFrame && HasCoordinateSystem())
            return output;

        if (GetPoseDiff() >= maxDiff)
            return output;

        if (mode_ >= HUAWEI_SFM)
            output = huawei->GetPointCloud();
        else
            output = google->GetPointCloud();

        return output;
    }

    std::vector<glm::mat4> ARCoreService::GetPose() {
        return GetPose(GetProjection(), GetView());

    }

    std::vector<glm::mat4> ARCoreService::GetPose(glm::mat4 projection, glm::mat4 view) {
        glm::vec3 scale;
        glm::quat rotation;
        glm::vec3 translation;
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(view, scale, rotation, translation, skew, perspective);

        GLCamera device;
        device.position = rotation * -translation;
        device.rotation = rotation;
        device.scale = glm::vec3(1);
        std::vector<glm::mat4> output;
        output.push_back(glm::rotate(device.GetTransformation(), glm::radians(180.0f), glm::vec3(1, 0, 0)));
        output.push_back(device.GetTransformation());
        output.push_back(projection * view);
        return output;
    }

    glm::mat4 ARCoreService::GetProjection() {
        if (mode_ >= HUAWEI_SFM)
            return huawei->GetProjection();
        else
            return google->GetProjection();
    }

    glm::mat4 ARCoreService::GetView() {
        if (mode_ >= HUAWEI_SFM)
            return huawei->GetView();
        else
            return google->GetView();
    }

    bool ARCoreService::HasCoordinateSystem() {
        if (mode_ >= HUAWEI_SFM)
            return huawei->HasCoordinateSystem();
        else
            return google->HasCoordinateSystem();
    }

    glm::vec3 ARCoreService::HitTest(int x, int y) {
        if (mode_ >= HUAWEI_SFM)
            return huawei->HitTest(x, y);
        else
            return google->HitTest(x, y);
    }

    bool ARCoreService::IsFaceMode() {
        if (mode_ == GOOGLE_FACE)
            return true;
        else if (mode_ == HUAWEI_FACE)
            return true;
        else
            return false;
    }

    void ARCoreService::RemoveFaceDetails() {
        if (mode_ >= HUAWEI_SFM) {
            LOGE("RemoveFaceDetails on AREngine is unsupported");
        } else
            google->RemoveFaceDetails();
    }

    void ARCoreService::RenderCamera(int effect, int scale) {
        if (mode_ >= HUAWEI_SFM)
            huawei->RenderCamera((ARCoreCamera::Effect)effect, scale);
        else
            google->RenderCamera((ARCoreCamera::Effect)effect, scale);
    }

    void ARCoreService::SetNVScheme(ARCoreCamera::NightVisionScheme s) {
        if (mode_ >= HUAWEI_SFM)
            huawei->SetNVScheme(s);
        else
            google->SetNVScheme(s);
    }

    void ARCoreService::SetOffset(float offset) {
        if (mode_ >= HUAWEI_SFM)
            huawei->SetOffset(offset);
        else
            google->SetOffset(offset);
    }

    void ARCoreService::SetResolution(float res) {
        if (mode_ >= HUAWEI_SFM)
            huawei->SetResolution(res);
        else
            google->SetResolution(res);
    }

    Image *ARCoreService::GetDepthmap() {
        if (mode_ >= HUAWEI_SFM)
            return huawei->GetDepthMap(false, true, 1);
        else
            return google->GetDepthMap(false, true, 1);
    }

    std::vector<uint16_t> ARCoreService::GetLastDepthBuffer(int& width, int& height) {
        if (mode_ >= HUAWEI_SFM) {
            // TODO: Add depth buffer storage to AREngine if needed
            width = 0;
            height = 0;
            return std::vector<uint16_t>();
        } else {
            if (google) {
                return google->GetLastDepthBuffer(width, height);
            }
            width = 0;
            height = 0;
            return std::vector<uint16_t>();
        }
    }
}
