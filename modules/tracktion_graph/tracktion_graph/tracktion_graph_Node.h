/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2018
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

#pragma once

//==============================================================================
//==============================================================================
/**
    //==============================================================================
    Aims:
    - Separate graph structure from processing and any model
    - Ensure nodes can be processed multi-threaded which scales independantly of graph complexity
    - Processing can happen in any sized block (up to the maximum prepared for)
    - Process calls will only ever get the number of channels to fill that they report
    - Process calls will always provide empty buffers so nodes can simply "add" in to them. (Measure performance here)
    - Processing in float or double

    //==============================================================================
    Notes:
    - Each node should have pointers to its inputs
    - When a node is processed, it should check its inputs to see if they have produced outputs
    - If they have, that node can be processed. If they haven't the processor can try another node
    - If one node reports latency, every other node being summed with it will need to be delayed up to the same ammount
    - The reported latency of a node is the max of all its input latencies
 
    //==============================================================================
    Initialisation:
    - There really needs to be two stages for this:
        - Stage 1 is a simple visit of all the base nodes (i.e. no send/return connections).
          This will get a list of all the nodes in the graph
        - Stage 2 will be the initialise call with a flat list of all the nodes.
          Send/return nodes can use this to add inputs from the graph (e.g. based on busNum)
          (However, at this stage, return nodes might choose to add latency nodes to balance themselves???)
          (Maybe this should have to happen when the graph is built? That would mean latency needs to be fixed,
          and therefore plugins would need to be initialised before graph construction.)
    - Now we have a fully connected graph we can do a proper DFS:
        - This can be used to balance processing buffers between the graph by finding unconnected nodes
        - Finding a processing order by building "stacks" of nodes starting with leafs
    - This might look like the following steps:
        - visitAcyclic (Visitor) [only visit nodes that it was constructed with, used to create the flat list]
        - initialise (const PlaybackInitialisationInfo&) [used to add extra connections and balance latency etc.]
        - visitDFS (Visitor) [used to get list of nodes to process and can be called multiple times to optomise graph etc.]
 
    Avoiding incontinuities when latency changes:
    - One of the bg problems with this latency balancing approach is when the graph is rebuilt, any stateful buffers will be lost.
    - The only way I can think of solving this is to either reusue the latency nodes directly or reuse the buffers
    - The biggest problem here is finding which nodes correspond to the new and old graphs
    - The only way I can currently think of is:
        - Each node to have a UID when it is constructed based on its properties (source material, connections etc.)
        - During the initialisation stage, the old nodes can be passed on to the new nodes and if they have the same UID, the take any resources they need
        - There will probably need to be some validation to ensure that two nodes don't have the same UID
        - For things like clip/plugin nodes the UID is simple (EditItemID)
        - For latency nodes etc. it will probably have to be based on the input ID and channel index etc.
 
    Buffer optimisation:
    - As there will be a lot of nodes, it makes sense to reduce the memory footprint by reusing audio and MIDI buffers
    - There are two ways I can think of doing this:
        1. Analyse the graph and where nodes are sequential but not directly connected, use the same buffer
        2. Use a buffer pool and release it when all nodes have read from it.
            - This would probably require all nodes that need the output buffer to call "retain" on the node before the
              processing stage and then "release" once they're done with it. When the count drops to 0 the buffers can
              be released back to the pool
*/


namespace tracktion_graph
{

class Node;

//==============================================================================
/** Passed into Nodes when they are being initialised, to give them useful
    contextual information that they may need
*/
struct PlaybackInitialisationInfo
{
    double sampleRate;
    int blockSize;
    Node& rootNode;
    Node* rootNodeToReplace = nullptr;
};

/** Holds some really basic properties of a node */
struct NodeProperties
{
    bool hasAudio;
    bool hasMidi;
    int numberOfChannels;
    int latencyNumSamples = 0;
    size_t nodeID = 0;
};

//==============================================================================
//==============================================================================
/**
    Main graph Node processor class.
    Nodes are combined together to form a graph with a root Node which can then
    be initialsed and processed.
    
    Subclasses should implement the various virtual methods but never call these
    directly. They will be called automatically by the non-virtual methods.
*/
class Node
{
public:
    Node() = default;
    virtual ~Node() = default;
    
    //==============================================================================
    /** Call once after the graph has been constructed to initialise buffers etc. */
    void initialise (const PlaybackInitialisationInfo&);
    
    /** Call before processing the next block, used to reset the process status. */
    void prepareForNextBlock();
    
    /** Call to process the node, which will in turn call the process method with the
        buffers to fill.
        @param streamSampleRange The monotonic stream time in samples.
                                 This will be passed to the ProcessContext during the
                                 process callback so nodes can use this to determine file
                                 reading positions etc.
                                 Some nodes may ignore this completely but it should at the
                                 least specify the number to samples to process in this block.
    */
    void process (juce::Range<int64_t> streamSampleRange);
    
    /** Returns true if this node has processed and its outputs can be retrieved. */
    bool hasProcessed() const;
    
    /** Contains the buffers for a processing operation. */
    struct AudioAndMidiBuffer
    {
        juce::dsp::AudioBlock<float> audio;
        tracktion_engine::MidiMessageArray& midi;
    };

    /** Returns the processed audio and MIDI output.
        Must only be called after hasProcessed returns true.
    */
    AudioAndMidiBuffer getProcessedOutput();

    //==============================================================================
    /** Called after construction to give the node a chance to modify its topology.
        This should return true if any changes were made to the topology as this
        indicates that the method may need to be called again after other nodes have
        had their toplogy changed.
    */
    virtual bool transform (Node& /*rootNode*/) { return false; }
    
    /** Should return all the inputs directly feeding in to this node. */
    virtual std::vector<Node*> getDirectInputNodes() { return {}; }

    /** Should return the properties of the node.
        This should not be called until after initialise.
    */
    virtual NodeProperties getNodeProperties() = 0;

    /** Should return true when this node is ready to be processed.
        This is usually when its input's output buffers are ready.
    */
    virtual bool isReadyToProcess() = 0;
    
    /** Struct to describe a single iteration of a process call. */
    struct ProcessContext
    {
        juce::Range<int64_t> streamSampleRange;
        AudioAndMidiBuffer buffers;
    };
    
protected:
    /** Called once before playback begins for each node.
        Use this to allocate buffers etc.
        This step can be used to modify the topology of the graph (i.e. add/remove nodes).
        However, if you do this, you must make sure to call initialise on them so they are
        fully prepared for processing.
    */
    virtual void prepareToPlay (const PlaybackInitialisationInfo&) {}

    /** Called when the node is to be processed.
        This should add in to the buffers available making sure not to change their size at all.
    */
    virtual void process (const ProcessContext&) = 0;

private:
    std::atomic<bool> hasBeenProcessed { false };
    juce::AudioBuffer<float> audioBuffer;
    tracktion_engine::MidiMessageArray midiBuffer;
    int numSamplesProcessed = 0;
};

//==============================================================================
//==============================================================================
/** Should call the visitor for any direct inputs to the node exactly once.
    If preordering is true, nodes will be visited before their inputs, if
    false, inputs will be visited first.
 
    @param Visitor has the signature @code void (Node&) @endcode
*/
template<typename Visitor>
void visitNodes (Node&, Visitor&&, bool preordering);

//==============================================================================
/** Specifies the ordering algorithm. */
enum class VertexOrdering
{
    preordering,            // The order in which nodes are first visited
    postordering,           // The order in which nodes are last visited
    reversePreordering,     // The reverse of the preordering
    reversePostordering     // The reverse of the postordering
};

/** Returns all the nodes in a Node graph in the order given by vertexOrdering. */
static inline std::vector<Node*> getNodes (Node&, VertexOrdering);


//==============================================================================
//==============================================================================
/** Gives the graph a chance to modify its topology e.g. to connect send/return
    nodes and balance latency etc.
    Call this once after construction and it will call the Node::transform() method
    repeatedly for Node until they all return false indicating no topological
    changes have been made.
*/
static inline void transformNodes (Node& rootNode)
{
    for (;;)
    {
        bool needToTransformAgain = false;

        auto allNodes = getNodes (rootNode, VertexOrdering::postordering);

        for (auto node : allNodes)
            if (node->transform (rootNode))
                needToTransformAgain = true;

        if (! needToTransformAgain)
            break;
    }
}


//==============================================================================
//==============================================================================
inline void Node::initialise (const PlaybackInitialisationInfo& info)
{
    prepareToPlay (info);
    
    auto props = getNodeProperties();
    audioBuffer.setSize (props.numberOfChannels, info.blockSize);
}

inline void Node::prepareForNextBlock()
{
    hasBeenProcessed = false;
}

inline void Node::process (juce::Range<int64_t> streamSampleRange)
{
    audioBuffer.clear();
    midiBuffer.clear();
    const int numChannelsBeforeProcessing = audioBuffer.getNumChannels();
    const int numSamplesBeforeProcessing = audioBuffer.getNumSamples();
    juce::ignoreUnused (numChannelsBeforeProcessing, numSamplesBeforeProcessing);

    const int numSamples = (int) streamSampleRange.getLength();
    jassert (numSamples > 0); // This must be a valid number of samples to process
    
    auto inputBlock = numChannelsBeforeProcessing > 0 ? juce::dsp::AudioBlock<float> (audioBuffer).getSubBlock (0, (size_t) numSamples)
                                                      : juce::dsp::AudioBlock<float>();
    ProcessContext pc {
                        streamSampleRange,
                        { inputBlock , midiBuffer }
                      };
    process (pc);
    numSamplesProcessed = numSamples;
    hasBeenProcessed = true;
    
    jassert (numChannelsBeforeProcessing == audioBuffer.getNumChannels());
    jassert (numSamplesBeforeProcessing == audioBuffer.getNumSamples());
}

inline bool Node::hasProcessed() const
{
    return hasBeenProcessed;
}

inline Node::AudioAndMidiBuffer Node::getProcessedOutput()
{
    jassert (hasProcessed());
    return { juce::dsp::AudioBlock<float> (audioBuffer).getSubBlock (0, (size_t) numSamplesProcessed), midiBuffer };
}


//==============================================================================
//==============================================================================
namespace detail
{
    struct VisitNodesWithRecord
    {
        template<typename Visitor>
        static void visit (std::vector<Node*>& visitedNodes, Node& visitingNode, Visitor&& visitor, bool preordering)
        {
            if (std::find (visitedNodes.begin(), visitedNodes.end(), &visitingNode) != visitedNodes.end())
                return;
            
            if (preordering)
            {
                visitedNodes.push_back (&visitingNode);
                visitor (visitingNode);
            }

            for (auto n : visitingNode.getDirectInputNodes())
                visit  (visitedNodes, *n, visitor, preordering);

            if (! preordering)
            {
                visitedNodes.push_back (&visitingNode);
                visitor (visitingNode);
            }
        }
    };
}

template<typename Visitor>
inline void visitNodes (Node& node, Visitor&& visitor, bool preordering)
{
    std::vector<Node*> visitedNodes;
    detail::VisitNodesWithRecord::visit (visitedNodes, node, visitor, preordering);
}

inline std::vector<Node*> getNodes (Node& node, VertexOrdering vertexOrdering)
{
    bool preordering = vertexOrdering == VertexOrdering::preordering
                    || vertexOrdering == VertexOrdering::reversePreordering;
    
    std::vector<Node*> visitedNodes;
    detail::VisitNodesWithRecord::visit (visitedNodes, node, [](auto&){}, preordering);

    if (vertexOrdering == VertexOrdering::reversePreordering
        || vertexOrdering == VertexOrdering::reversePostordering)
       std::reverse (visitedNodes.begin(), visitedNodes.end());
    
    return visitedNodes;
}


}
