#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>
#include <vector>

namespace gridcollider
{
class StateGraphComponent final : public juce::Component
{
public:
    struct StateView
    {
        juce::String name;
        int gridCount = 1;
        int activeGrid = 0;
        double bpm = 120.0;
    };

    struct TransitionView
    {
        int fromState = 0;
        int toState = 0;
        double probability = 1.0;
        bool weighted = false;
    };

    StateGraphComponent();

    void paint(juce::Graphics& graphics) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;

    void setStates(std::vector<StateView> newStates, int newSelectedState, bool newPlaying);
    void setTransitions(std::vector<TransitionView> newTransitions);
    void setHatchAngle(int degrees);
    void fitToView();

    std::function<void(int)> onStateSelected;
    std::function<void()> onAddStateRequested;
    std::function<void()> onDeleteStateRequested;
    std::function<void()> onCopyStateRequested;
    std::function<void()> onPasteStateRequested;

private:
    std::vector<StateView> states;
    std::vector<TransitionView> transitions;
    int selectedState = 0;
    bool playing = false;
    int hatchAngle = 90;
    float zoom = 1.0f;
    float autoFitZoom = 1.0f;
    juce::Point<float> panOffset;
    std::optional<juce::Point<float>> rightDragPanAnchor;

    [[nodiscard]] std::vector<juce::Rectangle<float>> getNodeBounds(juce::Rectangle<float> area) const;
    [[nodiscard]] juce::Rectangle<float> getGraphBounds(juce::Rectangle<float> area) const;
    [[nodiscard]] int stateAt(juce::Point<float> position) const;
    [[nodiscard]] juce::Colour colourForState(int index) const;
    [[nodiscard]] juce::Point<float> toCanvasPoint(juce::Point<float> position) const noexcept;
    void drawTransition(juce::Graphics& graphics,
                        const TransitionView& transition,
                        const std::vector<juce::Rectangle<float>>& nodeBounds,
                        int siblingIndex,
                        int siblingCount) const;
    void zoomAt(float newZoom, juce::Point<float> anchor);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StateGraphComponent)
};
}
