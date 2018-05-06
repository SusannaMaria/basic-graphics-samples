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
    std::shared_ptr<magma::GraphicsPipeline> pipeline;

    const float fov = rapid::radians(60.f);
    rapid::matrix viewProj;
    bool negateViewport = false;

public:
    ParticlesApp(const AppEntry& entry):
        VulkanApp(entry, TEXT("14 - Particles"), 512, 512, true)
    {
        initialize();
        negateViewport = extensions->KHR_maintenance1 || extensions->AMD_negative_viewport_height;
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
        queue->submit(
            commandBuffers[bufferIndex],
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            presentFinished,
            renderFinished,
            waitFences[bufferIndex]);
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
        particles.reset(new ParticleSystem());
        particles->setMaxParticles(200);
        particles->setNumToRelease(10);
        particles->setReleaseInterval(0.05f);
        particles->setLifeCycle(5.0f);
        particles->setPosition(rapid::float3(0.0f, 0.0f, 0.0f));
        particles->setVelocity(rapid::float3(0.0f, 0.0f, 0.0f));
        particles->setGravity(rapid::float3(0.0f, -9.8f, 0.0f));
        particles->setWind(rapid::float3(0.0f, 0.0f, 0.0f));
        particles->setVelocityScale(20.0f);
        particles->setCollisionPlane(rapid::float3(0.0f, 1.0f, 0.0f), rapid::float3(0.0f, 0.0f, 0.0f));
        particles->initialize(device);
    }

    void setupView()
    {
        const rapid::vector3 eye(0.f, 3.f, 30.f);
        const rapid::vector3 center(0.f, 0.f, 0.f);
        const rapid::vector3 up(0.f, 1.f, 0.f);
        const float aspect = width/(float)height;
        const float zn = 1.f, zf = 100.f;
        const rapid::matrix view = rapid::lookAtRH(eye, center, up);
        const rapid::matrix proj = rapid::perspectiveFovRH(fov, aspect, zn, zf);
        viewProj = view * proj;
    }

    void updatePerspectiveTransform()
    {
        const float speed = 0.05f;
        static float angle = 0.f;
        angle += timer->millisecondsElapsed() * speed;
        const rapid::matrix world = rapid::rotationY(rapid::radians(spinX/2.f));
        magma::helpers::mapScoped<rapid::matrix>(uniformBuffer, true, [this, &world](auto *worldViewProj)
        {
            *worldViewProj = world * viewProj;
        });
    }

    void createUniformBuffer()
    {
        uniformBuffer.reset(new magma::UniformBuffer<rapid::matrix>(device));
    }

    void setupDescriptorSet()
    {
        // Create descriptor pool
        const uint32_t maxDescriptorSets = 1; // One set is enough for us
        descriptorPool.reset(new magma::DescriptorPool(device, maxDescriptorSets, {
            magma::descriptors::UniformBuffer(1), // Allocate simply one uniform buffer
        }));
        // Setup descriptor set layout:
        // Here we describe that slot 0 in vertex shader will have uniform buffer binding
        const magma::Descriptor uniformBufferDesc = magma::descriptors::UniformBuffer(1);
        descriptorSetLayout.reset(new magma::DescriptorSetLayout(device, {
            magma::bindings::VertexStageBinding(0, uniformBufferDesc)
        }));
        // Connect our uniform buffer to binding point
        descriptorSet = descriptorPool->allocateDescriptorSet(descriptorSetLayout);
        descriptorSet->update(0, uniformBuffer);
        // Specify push constant range
        const magma::pushconstants::VertexFragmentConstantRange<PushConstants> pushConstantRange;
        pipelineLayout.reset(new magma::PipelineLayout(descriptorSetLayout, {pushConstantRange}));
    }

    void setupPipeline()
    {
        const magma::VertexInputState vertexInput(
            magma::VertexInputBinding(0, sizeof(ParticleSystem::ParticleVertex)),
            {
                magma::VertexInputAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ParticleSystem::ParticleVertex, position)),
                magma::VertexInputAttribute(0, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ParticleSystem::ParticleVertex, color))
            }
        );
        pipeline.reset(new magma::GraphicsPipeline(device, pipelineCache,
            utilities::loadShaders(device, "pointSize.o", "particle.o"),
            vertexInput,
            magma::states::pointList,
            negateViewport ? magma::states::lineCullBackCW : magma::states::lineCullBackCCW,
            magma::states::dontMultisample,
            magma::states::depthAlwaysDontWrite,
            magma::states::blendNormalWriteRGB,
            {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR},
            pipelineLayout,
            renderPass));
    }

    void recordCommandBuffer(uint32_t index)
    {
        std::shared_ptr<magma::CommandBuffer> cmdBuffer = commandBuffers[index];
        cmdBuffer->begin();
        {
            cmdBuffer->setRenderArea(0, 0, width, height);
            cmdBuffer->beginRenderPass(renderPass, framebuffers[index],
                {
                    magma::ColorClear(0.0f, 0.0f, 0.0f, 1.f),
                    magma::DepthStencilClear(1.f, 0)
                }
            );
            {
                PushConstants pushConstants;
                pushConstants.width = static_cast<float>(width);
                pushConstants.height = static_cast<float>(height);
                pushConstants.h = (float)height/(2.f * tanf(fov * .5f)); // Scale with distance
                pushConstants.pointSize = .5f;

                cmdBuffer->setViewport(0, 0, width, negateViewport ? -height : height);
                cmdBuffer->setScissor(0, 0, width, height);
                cmdBuffer->bindDescriptorSet(pipelineLayout, descriptorSet);
                cmdBuffer->pushConstantBlock(pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, pushConstants);
                cmdBuffer->bindPipeline(pipeline);
                particles->draw(cmdBuffer);
            }
            cmdBuffer->endRenderPass();
        }
        cmdBuffer->end();
    }
};

std::unique_ptr<IApplication> appFactory(const AppEntry& entry)
{
    return std::unique_ptr<IApplication>(new ParticlesApp(entry));
}
