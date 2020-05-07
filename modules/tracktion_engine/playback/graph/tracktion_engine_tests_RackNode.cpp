/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2018
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

namespace tracktion_engine
{

#if TRACKTION_UNIT_TESTS

using namespace tracktion_graph;

//==============================================================================
//==============================================================================
class RackAudioNodeTests : public juce::UnitTest
{
public:
    RackAudioNodeTests()
        : juce::UnitTest ("RackNode", "tracktion_graph")
    {
    }
    
    void runTest() override
    {
        for (auto setup : test_utilities::getTestSetups (*this))
        {
            logMessage (String ("Test setup: sample rate SR, block size BS, random blocks RND")
                        .replace ("SR", String (setup.sampleRate))
                        .replace ("BS", String (setup.blockSize))
                        .replace ("RND", setup.randomiseBlockSizes ? "Y" : "N"));

            // Rack tests
            runRackTests (setup);
            runRackAudioInputTests (setup);
            runRackModifiertests (setup);
        }
    }

private:
    //==============================================================================
    void runRackTests (test_utilities::TestSetup testSetup)
    {
        using namespace tracktion_engine;
        auto& engine = *Engine::getEngines()[0];
        engine.getPluginManager().createBuiltInType<ToneGeneratorPlugin>();
        engine.getPluginManager().createBuiltInType<LatencyPlugin>();

        beginTest ("Unconnected Rack");
        {
            // Rack with a sin oscilator but not connected should be silent
            auto edit = Edit::createSingleTrackEdit (engine);
            auto track = getFirstAudioTrack (*edit);
            
            auto rack = edit->getRackList().addNewRack();
            expect (rack != nullptr);
            expectEquals (rack->getConnections().size(), 0);
            expectEquals (rack->getInputNames().size(), 3);
            expectEquals (rack->getOutputNames().size(), 3);

            Plugin::Ptr pluginPtr = edit->getPluginCache().createNewPlugin (ToneGeneratorPlugin::xmlTypeName, {});
            track->pluginList.insertPlugin (pluginPtr, 0, nullptr);
            auto tonePlugin = dynamic_cast<ToneGeneratorPlugin*> (pluginPtr.get());
            expect (tonePlugin != nullptr);

            rack->addPlugin (tonePlugin, {}, false);
            expect (rack->getPlugins().getFirst() == pluginPtr.get());

            // Process Rack
            {
                auto inputProvider = std::make_shared<InputProvider>();
                auto rackNode = RackNodeBuilder::createRackNode (*rack, inputProvider);

                auto rackProcessor = std::make_unique<RackNodePlayer> (std::move (rackNode), inputProvider, true);
                                        
                auto testContext = createTestContext (std::move (rackProcessor), testSetup, 2, 5.0);
                test_utilities::expectAudioBuffer (*this, testContext->buffer, 0, 0.0f, 0.0f);
            }
                    
            engine.getAudioFileManager().releaseAllFiles();
            edit->getTempDirectory (false).deleteRecursively();
        }

        beginTest ("Basic sin Rack");
        {
            auto edit = Edit::createSingleTrackEdit (engine);
            auto track = getFirstAudioTrack (*edit);
            Plugin::Ptr pluginPtr = edit->getPluginCache().createNewPlugin (ToneGeneratorPlugin::xmlTypeName, {});
            track->pluginList.insertPlugin (pluginPtr, 0, nullptr);
            auto tonePlugin = dynamic_cast<ToneGeneratorPlugin*> (pluginPtr.get());
            expect (tonePlugin != nullptr);
            
            Plugin::Array plugins;
            plugins.add (pluginPtr);
            auto rack = RackType::createTypeToWrapPlugins (plugins, *edit);
            expect (rack != nullptr);
            expect (rack->getPlugins().getFirst() == pluginPtr.get());
            expectEquals (rack->getConnections().size(), 6);
            
            // Process Rack
            {
                auto inputProvider = std::make_shared<InputProvider>();
                auto rackNode = RackNodeBuilder::createRackNode (*rack, inputProvider);

                auto rackProcessor = std::make_unique<RackNodePlayer> (std::move (rackNode), inputProvider, true);
                                        
                auto testContext = createTestContext (std::move (rackProcessor), testSetup, 2, 5.0);
                test_utilities::expectAudioBuffer (*this, testContext->buffer, 0, 1.0f, 0.707f);
            }
                    
            engine.getAudioFileManager().releaseAllFiles();
            edit->getTempDirectory (false).deleteRecursively();
        }

        beginTest ("Four channel sin Rack");
        {
            // This Rack has four input and output channels
            // The single sin node goes to all the outputs
            auto edit = Edit::createSingleTrackEdit (engine);
            auto track = getFirstAudioTrack (*edit);
            Plugin::Ptr pluginPtr = edit->getPluginCache().createNewPlugin (ToneGeneratorPlugin::xmlTypeName, {});
            track->pluginList.insertPlugin (pluginPtr, 0, nullptr);
            auto tonePlugin = dynamic_cast<ToneGeneratorPlugin*> (pluginPtr.get());
            expect (tonePlugin != nullptr);
            
            Plugin::Array plugins;
            plugins.add (pluginPtr);
            auto rack = RackType::createTypeToWrapPlugins (plugins, *edit);
            rack->addOutput (3, "Bus L");
            rack->addOutput (4, "Bus R");

            rack->addConnection (tonePlugin->itemID, 1, {}, 3);
            rack->addConnection (tonePlugin->itemID, 2, {}, 4);

            expectEquals (rack->getConnections().size(), 8);
            
            // Process Rack
            {
                auto inputProvider = std::make_shared<InputProvider> (2);
                auto rackNode = RackNodeBuilder::createRackNode (*rack, inputProvider);

                auto rackProcessor = std::make_unique<RackNodePlayer> (std::move (rackNode), inputProvider, true);
                                        
                auto testContext = createTestContext (std::move (rackProcessor), testSetup, 4, 5.0);
                
                for (int c : { 0, 1, 2, 3 })
                    test_utilities::expectAudioBuffer (*this, testContext->buffer, c, 1.0f, 0.707f);
            }
                    
            engine.getAudioFileManager().releaseAllFiles();
            edit->getTempDirectory (false).deleteRecursively();
        }
        
        beginTest ("Two sins in parallel Rack");
        {
            auto edit = Edit::createSingleTrackEdit (engine);
            auto track = getFirstAudioTrack (*edit);
            Plugin::Ptr pluginPtr = edit->getPluginCache().createNewPlugin (ToneGeneratorPlugin::xmlTypeName, {});
            track->pluginList.insertPlugin (pluginPtr, 0, nullptr);
            auto tonePlugin = dynamic_cast<ToneGeneratorPlugin*> (pluginPtr.get());
            tonePlugin->levelParam->setParameter (0.5f, dontSendNotification);
            expect (tonePlugin != nullptr);
            
            Plugin::Array plugins;
            plugins.add (pluginPtr);
            auto rack = RackType::createTypeToWrapPlugins (plugins, *edit);
            expect (rack != nullptr);
            
            // Add another ToneGenerator and connect it in parallel
            Plugin::Ptr secondToneGen = edit->getPluginCache().createNewPlugin (ToneGeneratorPlugin::xmlTypeName, {});
            dynamic_cast<ToneGeneratorPlugin*> (secondToneGen.get())->levelParam->setParameter (0.5f, dontSendNotification);
            rack->addPlugin (secondToneGen, {}, false);
            rack->addConnection ({}, 0, secondToneGen->itemID, 0);
            rack->addConnection ({}, 1, secondToneGen->itemID, 1);
            rack->addConnection ({}, 2, secondToneGen->itemID, 2);
            rack->addConnection (secondToneGen->itemID, 0, {}, 0);
            rack->addConnection (secondToneGen->itemID, 1, {}, 1);
            rack->addConnection (secondToneGen->itemID, 2, {}, 2);

            expectEquals (rack->getPlugins().size(), 2);
            expectEquals (rack->getConnections().size(), 12);
            
            // Process Rack
            {
                auto inputProvider = std::make_shared<InputProvider>();
                auto rackNode = RackNodeBuilder::createRackNode (*rack, inputProvider);

                auto rackProcessor = std::make_unique<RackNodePlayer> (std::move (rackNode), inputProvider, true);
                                        
                auto testContext = createTestContext (std::move (rackProcessor), testSetup, 2, 5.0);
                test_utilities::expectAudioBuffer (*this, testContext->buffer, 0, 1.0f, 0.707f);
            }
                    
            engine.getAudioFileManager().releaseAllFiles();
            edit->getTempDirectory (false).deleteRecursively();
        }

        beginTest ("Two sins in parallel, one delayed Rack");
        {
            auto edit = Edit::createSingleTrackEdit (engine);
            auto track = getFirstAudioTrack (*edit);
            Plugin::Ptr pluginPtr = edit->getPluginCache().createNewPlugin (ToneGeneratorPlugin::xmlTypeName, {});
            track->pluginList.insertPlugin (pluginPtr, 0, nullptr);
            auto tonePlugin = dynamic_cast<ToneGeneratorPlugin*> (pluginPtr.get());
            tonePlugin->levelParam->setParameter (0.5f, dontSendNotification);
            expect (tonePlugin != nullptr);
            
            Plugin::Array plugins;
            plugins.add (pluginPtr);
            auto rack = RackType::createTypeToWrapPlugins (plugins, *edit);
            expect (rack != nullptr);
            
            // Add another ToneGenerator feeding in to a LatencyPlugin and connect it in parallel
            Plugin::Ptr secondToneGen = edit->getPluginCache().createNewPlugin (ToneGeneratorPlugin::xmlTypeName, {});
            dynamic_cast<ToneGeneratorPlugin*> (secondToneGen.get())->levelParam->setParameter (0.5f, dontSendNotification);

            Plugin::Ptr latencyPlugin = edit->getPluginCache().createNewPlugin (LatencyPlugin::xmlTypeName, {});
            const double latencyTimeInSeconds = 0.5f;
            dynamic_cast<LatencyPlugin*> (latencyPlugin.get())->latencyTimeSeconds = latencyTimeInSeconds;

            rack->addPlugin (secondToneGen, {}, false);
            rack->addPlugin (latencyPlugin, {}, false);

            rack->addConnection ({}, 0, secondToneGen->itemID, 0);
            rack->addConnection ({}, 1, secondToneGen->itemID, 1);
            rack->addConnection ({}, 2, secondToneGen->itemID, 2);
            rack->addConnection (secondToneGen->itemID, 0, latencyPlugin->itemID, 0);
            rack->addConnection (secondToneGen->itemID, 1, latencyPlugin->itemID, 1);
            rack->addConnection (secondToneGen->itemID, 2, latencyPlugin->itemID, 2);
            rack->addConnection (latencyPlugin->itemID, 0, {}, 0);
            rack->addConnection (latencyPlugin->itemID, 1, {}, 1);
            rack->addConnection (latencyPlugin->itemID, 2, {}, 2);

            expectEquals (rack->getPlugins().size(), 3);
            expectEquals (rack->getConnections().size(), 15);
            
            // Process Rack
            {
                auto inputProvider = std::make_shared<InputProvider>();
                auto rackNode = RackNodeBuilder::createRackNode (*rack, inputProvider);

                auto rackProcessor = std::make_unique<RackNodePlayer> (std::move (rackNode), inputProvider, true);
                                        
                auto testContext = createTestContext (std::move (rackProcessor), testSetup, 2, 5.0);
                const int latencyNumSamples = roundToInt (latencyTimeInSeconds * testSetup.sampleRate);
                test_utilities::expectAudioBuffer (*this, testContext->buffer, 0, latencyNumSamples, 0.0f, 0.0f, 1.0f, 0.707f);
                test_utilities::expectAudioBuffer (*this, testContext->buffer, 1, latencyNumSamples, 0.0f, 0.0f, 1.0f, 0.707f);
            }
                    
            engine.getAudioFileManager().releaseAllFiles();
            edit->getTempDirectory (false).deleteRecursively();
        }
    }

    void runRackAudioInputTests (test_utilities::TestSetup testSetup)
    {
        using namespace tracktion_engine;
        auto& engine = *Engine::getEngines()[0];
        
        // These tests won't work with random block sizes as the test inputs are just static
        if (! testSetup.randomiseBlockSizes)
        {
            beginTest ("Basic sin audio input Rack");
            {
                // Just a stereo sin input connected directly to the output across 4 channels
                auto edit = Edit::createSingleTrackEdit (engine);

                Plugin::Array plugins;
                auto rack = edit->getRackList().addNewRack();
                expect (rack != nullptr);
                
                rack->addInput (3, "Bus In L");
                rack->addInput (4, "Bus In R");
                rack->addOutput (3, "Bus Out L");
                rack->addOutput (4, "Bus Out R");

                for (int p : { 0, 1, 2, 3, 4 })
                    rack->addConnection ({}, p, {}, p);
                
                expectEquals (rack->getConnections().size(), 5);

                // Sin input provider
                const auto inputProvider = std::make_shared<InputProvider>();
                AudioBuffer<float> inputBuffer (4, testSetup.blockSize);

                // Fill inputs with sin data
                {
                    test_utilities::fillBufferWithSinData (inputBuffer);
                    tracktion_engine::MidiMessageArray midi;
                    inputProvider->setInputs ({ juce::dsp::AudioBlock<float> (inputBuffer), midi });
                }

                // Process Rack
                {
                    auto rackNode = RackNodeBuilder::createRackNode (*rack, inputProvider);
                    auto rackProcessor = std::make_unique<RackNodePlayer> (std::move (rackNode), inputProvider, false);
                    auto testContext = createTestContext (std::move (rackProcessor), testSetup, 4, 5.0);

                    for (int c : { 0, 1, 2, 3 })
                        test_utilities::expectAudioBuffer (*this, testContext->buffer, c, 1.0f, 0.707f);
                }
                
                // Remove connections between 3 & 4, add a latency plugin there, the results should be the same
                {
                    rack->removeConnection ({}, 3, {}, 3);
                    rack->removeConnection ({}, 4, {}, 4);

                    Plugin::Ptr latencyPlugin = edit->getPluginCache().createNewPlugin (LatencyPlugin::xmlTypeName, {});
                    const double latencyTimeInSeconds = 0.5f;
                    const int latencyNumSamples = roundToInt (latencyTimeInSeconds * testSetup.sampleRate);
                    dynamic_cast<LatencyPlugin*> (latencyPlugin.get())->latencyTimeSeconds = latencyTimeInSeconds;

                    rack->addPlugin (latencyPlugin, {}, false);

                    rack->addConnection ({}, 3, latencyPlugin->itemID, 1);
                    rack->addConnection ({}, 4, latencyPlugin->itemID, 2);
                    rack->addConnection (latencyPlugin->itemID, 1, {}, 3);
                    rack->addConnection (latencyPlugin->itemID, 2, {}, 4);

                    expectEquals (rack->getConnections().size(), 7);

                    // Process Rack
                    {
                        auto rackNode = RackNodeBuilder::createRackNode (*rack, inputProvider);
                        auto rackProcessor = std::make_unique<RackNodePlayer> (std::move (rackNode), inputProvider, false);
                        auto testContext = createTestContext (std::move (rackProcessor), testSetup, 4, 5.0);

                        for (int c : { 0, 1, 2, 3 })
                            test_utilities::expectAudioBuffer (*this, testContext->buffer, c, latencyNumSamples, 0.0f, 0.0f, 1.0f, 0.707f);
                    }

                    // Set the num audio inputs to be 1 channel and the Rack shouldn't crash
                    {
                        inputProvider->numChannels = 1;
                        tracktion_engine::MidiMessageArray midi;
                        inputProvider->setInputs ({ juce::dsp::AudioBlock<float> (inputBuffer), midi });

                        auto rackNode = RackNodeBuilder::createRackNode (*rack, inputProvider);
                        auto rackProcessor = std::make_unique<RackNodePlayer> (std::move (rackNode), inputProvider, false);
                        auto testContext = createTestContext (std::move (rackProcessor), testSetup, 4, 5.0);

                        // Channel 0 should be a sin from 0.5s, silent before
                        test_utilities::expectAudioBuffer (*this, testContext->buffer, 0, latencyNumSamples,
                                                           0.0f, 0.0f, 1.0f, 0.707f);

                        // The others should be silent
                        for (int c : { 1, 2, 3 })
                            test_utilities::expectAudioBuffer (*this, testContext->buffer, c, 0.0f, 0.0f);
                    }
                }
                        
                engine.getAudioFileManager().releaseAllFiles();
                edit->getTempDirectory (false).deleteRecursively();
            }
            
            beginTest ("Mismatched num input and Rack channels");
            {
                // Just a stereo sin input connected directly to the output across 2 channels
                auto edit = Edit::createSingleTrackEdit (engine);

                Plugin::Array plugins;
                auto rack = edit->getRackList().addNewRack();
                expect (rack != nullptr);
                                    
                for (int p : { 0, 1, 2 })
                    rack->addConnection ({}, p, {}, p);

                expectEquals (rack->getConnections().size(), 3);

                // Sin input provider
                const auto inputProvider = std::make_shared<InputProvider>();
                AudioBuffer<float> inputBuffer (1, testSetup.blockSize);

                // Fill inputs with sin data
                {
                    test_utilities::fillBufferWithSinData (inputBuffer);
                    tracktion_engine::MidiMessageArray midi;
                    inputProvider->setInputs ({ juce::dsp::AudioBlock<float> (inputBuffer), midi });
                }

                // Process Rack
                {
                    auto rackNode = RackNodeBuilder::createRackNode (*rack, inputProvider);
                    auto rackProcessor = std::make_unique<RackNodePlayer> (std::move (rackNode), inputProvider, false);
                    auto testContext = createTestContext (std::move (rackProcessor), testSetup, 2, 5.0);

                    // Channel 0 should be a sin, channel 1 silent
                    test_utilities::expectAudioBuffer (*this, testContext->buffer, 0, 1.0f, 0.707f);
                    test_utilities::expectAudioBuffer (*this, testContext->buffer, 1, 0.0f, 0.0f);
                }
                        
                engine.getAudioFileManager().releaseAllFiles();
                edit->getTempDirectory (false).deleteRecursively();
            }
        }
    }
    
    void runRackModifiertests (test_utilities::TestSetup ts)
    {
        using namespace tracktion_engine;
        auto& engine = *Engine::getEngines()[0];
        
        beginTest ("LFO Modifier Rack");
        {
            auto edit = Edit::createSingleTrackEdit (engine);
            auto track = getFirstAudioTrack (*edit);
            Plugin::Ptr pluginPtr = edit->getPluginCache().createNewPlugin (ToneGeneratorPlugin::xmlTypeName, {});
            track->pluginList.insertPlugin (pluginPtr, 0, nullptr);
            auto tonePlugin = dynamic_cast<ToneGeneratorPlugin*> (pluginPtr.get());
            expect (tonePlugin != nullptr);
            
            Plugin::Array plugins;
            plugins.add (pluginPtr);
            auto rack = RackType::createTypeToWrapPlugins (plugins, *edit);
            expect (rack != nullptr);
            expect (rack->getPlugins().getFirst() == pluginPtr.get());
            expectEquals (rack->getConnections().size(), 6);
            
            auto modifier = rack->getModifierList().insertModifier (ValueTree (IDs::LFO), 0, nullptr);
            auto lfoModifier = dynamic_cast<LFOModifier*> (modifier.get());
            lfoModifier->depthParam->setParameter (0.0f, dontSendNotification);
            lfoModifier->offsetParam->setParameter (0.5f, dontSendNotification);
            expectWithinAbsoluteError (lfoModifier->depthParam->getCurrentValue(), 0.0f, 0.001f);
            expectWithinAbsoluteError (lfoModifier->offsetParam->getCurrentValue(), 0.5f, 0.001f);
            
            tonePlugin->levelParam->addModifier (*modifier, -1.0f);
            
            PlayHead playhead;
            edit->updateModifierTimers (playhead, EditTimeRange(), 0);
            tonePlugin->levelParam->updateToFollowCurve (0.0); // Force an update of the param value for testing
            expectWithinAbsoluteError (lfoModifier->getCurrentValue(), 0.5f, 0.001f);
            expectWithinAbsoluteError (tonePlugin->levelParam->getCurrentValue(), 0.5f, 0.001f);

            // Process Rack
            {
                auto inputProvider = std::make_shared<InputProvider>();
                auto rackNode = RackNodeBuilder::createRackNode (*rack, inputProvider);

                auto rackProcessor = std::make_unique<RackNodePlayer> (std::move (rackNode), inputProvider, true);
                                        
                auto testContext = createTestContext (std::move (rackProcessor), ts, 2, 5.0);
                test_utilities::expectAudioBuffer (*this, testContext->buffer, 0, 0.5f, 0.353f);
            }

            // Check this hasn't changed
            expectWithinAbsoluteError (tonePlugin->levelParam->getCurrentValue(), 0.5f, 0.001f);

            engine.getAudioFileManager().releaseAllFiles();
            edit->getTempDirectory (false).deleteRecursively();
        }
    }
};

static RackAudioNodeTests rackAudioNodeTests;

 #endif //TRACKTION_UNIT_TESTS

}
