#include <fstream>
#include "../framework/vulkanApp.h"
#include "../framework/utilities.h"
#include "watchdog.h"

class ShaderToyApp : public VulkanApp
{
    struct alignas(16) BuiltInUniforms
    {
        rapid::float2 iResolution;
        rapid::float2 iMouse;
        float iTime;
    };

    std::unique_ptr<FileWatchdog> watchdog;
    std::unique_ptr<magma::aux::ShaderCompiler> glslCompiler;
    std::shared_ptr<magma::ShaderModule> vertexShader;
    std::shared_ptr<magma::ShaderModule> fragmentShader;
    std::shared_ptr<magma::UniformBuffer<BuiltInUniforms>> builtinUniforms;
    std::shared_ptr<magma::DescriptorSetLayout> descriptorSetLayout;
    std::shared_ptr<magma::DescriptorSet> descriptorSet;
    std::shared_ptr<magma::DescriptorPool> descriptorPool;
    std::shared_ptr<magma::PipelineLayout> pipelineLayout;
    std::shared_ptr<magma::GraphicsPipeline> graphicsPipeline;

    std::atomic<bool> rebuildCommandBuffers = false;
    int mouseX = 0;
    int mouseY = 0;
    bool dragging = false;

public:
    ShaderToyApp(const AppEntry& entry):
        VulkanApp(entry, TEXT("17 - ShaderToy"), 512, 512)
    {
        initialize();
        vertexShader = compileShader("quad.vert");
        fragmentShader = compileShader("shader.frag");
        initializeWatchdog();
        createUniformBuffer();
        setupDescriptorSet();
        setupPipeline();
        recordCommandBuffer(FrontBuffer);
        recordCommandBuffer(BackBuffer);
        timer->run();
    }

    virtual void render(uint32_t bufferIndex) override
    {
        if (rebuildCommandBuffers)
        {
            waitFences[1 - bufferIndex]->wait();
            setupPipeline();
            recordCommandBuffer(FrontBuffer);
            recordCommandBuffer(BackBuffer);
            rebuildCommandBuffers = false;
        }
        updateUniforms();
        submitCmdBuffer(bufferIndex);
    }

    virtual void onMouseMove(int x, int y) override
    {
        if (dragging)
        {
            mouseX = x;
            mouseY = y;
        }
    }

    virtual void onMouseLButton(bool down, int x, int y) override
    {
        dragging = down;
        if (dragging)
        {
            mouseX = x;
            mouseY = y;
        }
    }

    void updateUniforms()
    {
        magma::helpers::mapScoped<BuiltInUniforms>(builtinUniforms, true, [this](auto *builtin)
        {
            static float totalTime = 0.0f;
            totalTime += timer->secondsElapsed();
            builtin->iResolution.x = static_cast<float>(width);
            builtin->iResolution.y = static_cast<float>(height);
            builtin->iMouse.x = static_cast<float>(mouseX);
            builtin->iMouse.y = static_cast<float>(mouseY);
            builtin->iTime = totalTime;
        });
    }

    std::shared_ptr<magma::ShaderModule> compileShader(const std::string& filename)
    {
        std::shared_ptr<magma::ShaderModule> shaderModule;
        std::ifstream file(filename);
        if (file.is_open())
        {
            std::string source((std::istreambuf_iterator<char>(file)),
                std::istreambuf_iterator<char>());
            std::cout << "compiling shader \"" << filename << "\"" << std::endl;
            shaderc_shader_kind shaderKind;
            if (filename.find(".vert") != std::string::npos)
                shaderKind = shaderc_glsl_default_vertex_shader;
            else
                shaderKind = shaderc_glsl_default_fragment_shader;
            if (!glslCompiler)
                glslCompiler = std::make_unique<magma::aux::ShaderCompiler>(device, nullptr);
            shaderModule = glslCompiler->compileShader(source, "main", shaderKind);
        }
        else
        {
            throw std::runtime_error("failed to open file \"" + std::string(filename) + "\"");
        }
        return shaderModule;
    }

    void initializeWatchdog()
    {
        auto onModified = [this](const std::string& filename) -> void
        {
            try
            {
                if (filename.find(".vert") != std::string::npos)
                    vertexShader = compileShader(filename);
                else
                    fragmentShader = compileShader(filename);
                rebuildCommandBuffers = true;
            } catch (const std::exception& exception)
            {
                std::cout << exception.what();
            }
        };

        const uint32_t pollFrequencyMs = 500;
        watchdog = std::make_unique<FileWatchdog>(pollFrequencyMs);
        watchdog->watchFor("quad.vert", onModified);
        watchdog->watchFor("shader.frag", onModified);
    }

    void createUniformBuffer()
    {
        builtinUniforms = std::make_shared<magma::UniformBuffer<BuiltInUniforms>>(device);
    }

    void setupDescriptorSet()
    {
        constexpr uint32_t maxDescriptorSets = 1;
        const magma::Descriptor uniformBufferDesc = magma::descriptors::UniformBuffer(1);
        descriptorPool = std::make_shared<magma::DescriptorPool>(device, maxDescriptorSets,
            std::vector<magma::Descriptor>
            {
                uniformBufferDesc,
            });
        descriptorSetLayout = std::make_shared<magma::DescriptorSetLayout>(device,
            std::initializer_list<magma::DescriptorSetLayout::Binding>
            {   // Bind built-in uniforms to slot 0 in fragment shader
                magma::bindings::FragmentStageBinding(0, uniformBufferDesc),
            });
        descriptorSet = descriptorPool->allocateDescriptorSet(descriptorSetLayout);
        descriptorSet->update(0, builtinUniforms);
        pipelineLayout = std::make_shared<magma::PipelineLayout>(descriptorSetLayout);
    }

    void setupPipeline()
    {
        graphicsPipeline = std::make_shared<magma::GraphicsPipeline>(device, pipelineCache,
            std::vector<magma::PipelineShaderStage>
            {
                magma::VertexShaderStage(vertexShader, "main"),
                magma::FragmentShaderStage(fragmentShader, "main")
            },
            magma::renderstates::nullVertexInput,
            magma::renderstates::triangleStrip,
            magma::TesselationState(),
            magma::ViewportState(0, 0, width, height),
            magma::renderstates::fillCullBackCCW,
            magma::renderstates::noMultisample,
            magma::renderstates::depthAlwaysDontWrite,
            magma::renderstates::dontBlendWriteRGB,
            std::initializer_list<VkDynamicState>{},
            pipelineLayout,
            renderPass);
    }

    void recordCommandBuffer(uint32_t index)
    {
        std::shared_ptr<magma::CommandBuffer> cmdBuffer = commandBuffers[index];
        cmdBuffer->begin();
        {
            cmdBuffer->setRenderArea(0, 0, width, height);
            cmdBuffer->beginRenderPass(renderPass, framebuffers[index], {magma::clears::grayColor});
            {
                cmdBuffer->bindDescriptorSet(pipelineLayout, descriptorSet);
                cmdBuffer->bindPipeline(graphicsPipeline);
                cmdBuffer->draw(4, 0);
            }
            cmdBuffer->endRenderPass();
        }
        cmdBuffer->end();
    }
};

std::unique_ptr<IApplication> appFactory(const AppEntry& entry)
{
    return std::make_unique<ShaderToyApp>(entry);
}
