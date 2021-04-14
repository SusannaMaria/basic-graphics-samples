#include "../framework/vulkanApp.h"
#include "particlesystem.h"

// Use Space to reset particles + mouse to rotate scene
class ParticlesApp : public VulkanApp
{
    struct PushConstants
    {
        float width;
        float height;
        float h;
        float pointSize;
    };

    std::unique_ptr<ParticleSystem> particles;
    std::shared_ptr<magma::UniformBuffer<rapid::matrix>> uniformBuffer;
    std::shared_ptr<magma::DescriptorPool> descriptorPool;
    std::shared_ptr<magma::DescriptorSetLayout> descriptorSetLayout;
    std::shared_ptr<magma::DescriptorSet> descriptorSet;
    std::shared_ptr<magma::PipelineLayout> pipelineLayout;
    std::shared_ptr<magma::GraphicsPipeline> graphicsPipeline;

    static constexpr float fov = rapid::radians(60.f);
    rapid::matrix viewProj;

public:
    ParticlesApp(const AppEntry& entry):
        VulkanApp(entry, TEXT("15 - Particles"), 512, 512, true)
    {
        initialize();
        initParticleSystem();
        setupView();
        createUniformBuffer();
        setupDescriptorSet();
        setupPipeline();
        recordCommandBuffer(FrontBuffer);
        recordCommandBuffer(BackBuffer);
        timer->run();
    }

    virtual void render(uint32_t bufferIndex) override
    {
        particles->update(timer->secondsElapsed());
        updatePerspectiveTransform();
        submitCommandBuffer(bufferIndex);
    }

    virtual void onKeyDown(char key, int repeat, uint32_t flags) override
    {
        switch (key)
        {
        case AppKey::Space:
            particles->reset();
            break;
        }
        VulkanApp::onKeyDown(key, repeat, flags);
    }

    void initParticleSystem()
    {
        particles = std::unique_ptr<ParticleSystem>(new ParticleSystem());
        particles->setMaxParticles(200);
        particles->setNumToRelease(10);
        particles->setReleaseInterval(0.05f);
        particles->setLifeCycle(5.0f);
        particles->setPosition(rapid::float3(0.f, 0.f, 0.f));
        particles->setVelocity(rapid::float3(0.f, 0.f, 0.f));
        particles->setGravity(rapid::float3(0.f, -9.8f, 0.f));
        particles->setWind(rapid::float3(0.f, 0.f, 0.f));
        particles->setVelocityScale(20.f);
        particles->setCollisionPlane(rapid::float3(0.f, 1.f, 0.f), rapid::float3(0.f, 0.f, 0.f));
        particles->initialize(device);
    }

    void setupView()
    {
        const rapid::vector3 eye(0.f, 3.f, 30.f);
        const rapid::vector3 center(0.f, 0.f, 0.f);
        const rapid::vector3 up(0.f, 1.f, 0.f);
        const float aspect = width/(float)height;
        constexpr float zn = 1.f, zf = 100.f;
        const rapid::matrix view = rapid::lookAtRH(eye, center, up);
        const rapid::matrix proj = rapid::perspectiveFovRH(fov, aspect, zn, zf);
        viewProj = view * proj;
    }

    void updatePerspectiveTransform()
    {
        constexpr float speed = 0.05f;
        static float angle = 0.f;
        angle += timer->millisecondsElapsed() * speed;
        const rapid::matrix world = rapid::rotationY(rapid::radians(spinX/2.f));
        magma::helpers::mapScoped(uniformBuffer,
            [this, &world](auto *worldViewProj)
            {
                *worldViewProj = world * viewProj;
            });
    }

    void createUniformBuffer()
    {
        uniformBuffer = std::make_shared<magma::UniformBuffer<rapid::matrix>>(device);
    }

    void setupDescriptorSet()
    {
        constexpr magma::Descriptor oneUniformBuffer = magma::descriptors::UniformBuffer(1);
        descriptorPool = std::make_shared<magma::DescriptorPool>(device, 1, oneUniformBuffer);
        descriptorSetLayout = std::make_shared<magma::DescriptorSetLayout>(device,
            magma::bindings::VertexStageBinding(0, oneUniformBuffer));
        descriptorSet = descriptorPool->allocateDescriptorSet(descriptorSetLayout);
        descriptorSet->writeDescriptor(0, uniformBuffer);
        constexpr magma::pushconstants::VertexFragmentConstantRange<PushConstants> pushConstantRange;
        pipelineLayout = std::make_shared<magma::PipelineLayout>(descriptorSetLayout, pushConstantRange);
    }

    void setupPipeline()
    {
        const magma::VertexInputStructure<ParticleSystem::ParticleVertex> vertexInput(0, {
            {0, &ParticleSystem::ParticleVertex::position},
            {1, &ParticleSystem::ParticleVertex::color}});
        graphicsPipeline = std::make_shared<GraphicsPipeline>(device,
            "pointSize.o",
            negateViewport ? "particleNeg.o" : "particle.o",
            vertexInput,
            magma::renderstates::pointList,
            negateViewport ? magma::renderstates::lineCullBackCW : magma::renderstates::lineCullBackCCW,
            magma::renderstates::dontMultisample,
            magma::renderstates::depthAlwaysDontWrite,
            magma::renderstates::blendNormalRgb,
            pipelineLayout,
            renderPass, 0,
            pipelineCache);
    }

    void recordCommandBuffer(uint32_t index)
    {
        std::shared_ptr<magma::CommandBuffer> cmdBuffer = commandBuffers[index];
        cmdBuffer->begin();
        {
            cmdBuffer->beginRenderPass(renderPass, framebuffers[index],
                {
                    magma::clears::blackColor,
                    magma::clears::depthOne
                });
            {
                PushConstants pushConstants;
                pushConstants.width = static_cast<float>(width);
                pushConstants.height = static_cast<float>(height);
                pushConstants.h = (float)height/(2.f * tanf(fov * .5f)); // Scale with distance
                pushConstants.pointSize = .5f;

                cmdBuffer->setViewport(0, 0, width, negateViewport ? -height : height);
                cmdBuffer->setScissor(0, 0, width, height);
                cmdBuffer->bindDescriptorSet(graphicsPipeline, descriptorSet);
                cmdBuffer->pushConstantBlock(pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, pushConstants);
                cmdBuffer->bindPipeline(graphicsPipeline);
                particles->draw(cmdBuffer);
            }
            cmdBuffer->endRenderPass();
        }
        cmdBuffer->end();
    }
};

std::unique_ptr<IApplication> appFactory(const AppEntry& entry)
{
    return std::unique_ptr<ParticlesApp>(new ParticlesApp(entry));
}
