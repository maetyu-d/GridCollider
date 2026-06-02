#include "StateGraphComponent.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace gridcollider
{
namespace
{
constexpr float minimumZoom = 0.45f;
constexpr float maximumZoom = 3.0f;
constexpr float zoomStep = 1.12f;

[[nodiscard]] juce::Colour paper() noexcept { return juce::Colour::fromRGB(24, 25, 23); }
[[nodiscard]] juce::Colour panel() noexcept { return juce::Colour::fromRGB(34, 36, 33); }
[[nodiscard]] juce::Colour ink() noexcept { return juce::Colour::fromRGB(226, 230, 216); }

[[nodiscard]] juce::Colour accent(const int index) noexcept
{
    static constexpr std::array<std::uint32_t, 8> colours {
        0xff608ec4, 0xffd36c38, 0xffcdb246, 0xff6fbb70,
        0xffb98d5a, 0xff8d78ad, 0xff77a99a, 0xffb0b6a6
    };
    return juce::Colour(colours[static_cast<std::size_t>(juce::jlimit(0, 7, index % 8))]);
}

[[nodiscard]] juce::Point<float> pointOnEllipse(const juce::Point<float> centre,
                                                const float radiusX,
                                                const float radiusY,
                                                const float angle) noexcept
{
    return { centre.x + std::cos(angle) * radiusX,
             centre.y + std::sin(angle) * radiusY };
}

void drawHatch(juce::Graphics& graphics,
               juce::Rectangle<float> area,
               const int angleDegrees,
               const float spacing,
               const float alpha)
{
    if (area.isEmpty())
        return;

    graphics.saveState();
    graphics.reduceClipRegion(area.toNearestInt());

    juce::ignoreUnused(angleDegrees);
    const auto step = juce::jmax(22.0f, spacing * 3.0f);

    graphics.setColour(ink().withAlpha(alpha * 0.18f));
    for (auto y = area.getY(); y < area.getBottom(); y += step)
        graphics.drawHorizontalLine(juce::roundToInt(y), area.getX(), area.getRight());

    graphics.setColour(ink().withAlpha(alpha * 0.10f));
    for (auto x = area.getX(); x < area.getRight(); x += step)
        graphics.drawVerticalLine(juce::roundToInt(x), area.getY(), area.getBottom());

    graphics.restoreState();
}
}

StateGraphComponent::StateGraphComponent()
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void StateGraphComponent::setStates(std::vector<StateView> newStates, const int newSelectedState, const bool newPlaying)
{
    const auto previousCount = states.size();
    states = std::move(newStates);
    selectedState = juce::jlimit(0, juce::jmax(0, static_cast<int>(states.size()) - 1), newSelectedState);
    playing = newPlaying;

    if (states.size() != previousCount)
        fitToView();

    repaint();
}

void StateGraphComponent::setTransitions(std::vector<TransitionView> newTransitions)
{
    transitions = std::move(newTransitions);
    repaint();
}

void StateGraphComponent::setHatchAngle(const int degrees)
{
    hatchAngle = degrees;
    repaint();
}

void StateGraphComponent::fitToView()
{
    const auto area = getLocalBounds().toFloat().reduced(10.0f);

    if (area.getWidth() <= 0.0f || area.getHeight() <= 0.0f || states.empty())
        return;

    const auto graphBounds = getGraphBounds(area);

    if (graphBounds.isEmpty())
        return;

    const auto fitX = area.getWidth() / graphBounds.getWidth();
    const auto fitY = area.getHeight() / graphBounds.getHeight();
    autoFitZoom = juce::jlimit(minimumZoom, 1.0f, juce::jmin(fitX, fitY) * 0.94f);
    zoom = autoFitZoom;
    panOffset = area.getCentre() - graphBounds.getCentre();
    repaint();
}

void StateGraphComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(paper());

    const auto componentArea = getLocalBounds().toFloat().reduced(10.0f);
    auto area = componentArea;
    drawHatch(graphics, area, hatchAngle, 8.0f, 0.10f);

    graphics.saveState();
    graphics.reduceClipRegion(componentArea.toNearestInt());
    graphics.addTransform(juce::AffineTransform::translation(-componentArea.getCentreX(), -componentArea.getCentreY())
                              .scaled(zoom)
                              .translated(componentArea.getCentreX() + panOffset.x,
                                          componentArea.getCentreY() + panOffset.y));

    const auto nodeBounds = getNodeBounds(area);

    if (nodeBounds.empty())
        return;

    for (int index = 0; index < static_cast<int>(transitions.size()); ++index)
    {
        const auto& transition = transitions[static_cast<std::size_t>(index)];
        int siblingIndex = 0;
        int siblingCount = 0;

        for (const auto& candidate : transitions)
        {
            if (candidate.fromState == transition.fromState && candidate.toState == transition.toState)
            {
                if (&candidate == &transition)
                    siblingIndex = siblingCount;

                ++siblingCount;
            }
        }

        drawTransition(graphics, transition, nodeBounds, siblingIndex, juce::jmax(1, siblingCount));
    }

    for (int index = 0; index < static_cast<int>(nodeBounds.size()); ++index)
    {
        const auto bounds = nodeBounds[static_cast<std::size_t>(index)];
        const auto selected = index == selectedState;
        graphics.setColour(panel().withAlpha(selected ? 0.98f : 0.84f));
        graphics.fillEllipse(bounds);

        if (selected)
        {
            graphics.setColour(accent(index).withAlpha(0.42f));
            graphics.drawEllipse(bounds.expanded(7.0f), 1.2f);
            graphics.setColour(accent(index).withAlpha(0.24f));
            graphics.drawEllipse(bounds.expanded(14.0f), 1.0f);
        }

        graphics.setColour(selected ? accent(index) : ink().withAlpha(0.46f));
        graphics.drawEllipse(bounds, selected ? 2.0f : 1.0f);

        const auto gridCount = juce::jlimit(1, 8, states[static_cast<std::size_t>(index)].gridCount);

        graphics.setFont(juce::FontOptions(juce::Font::getDefaultSansSerifFontName(), selected ? 18.0f : 16.0f, juce::Font::bold));
        graphics.setColour(ink());
        graphics.drawFittedText(states[static_cast<std::size_t>(index)].name,
                                bounds.withTrimmedTop(bounds.getHeight() * 0.24f).toNearestInt(),
                                juce::Justification::centred,
                                1);

        graphics.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
        graphics.setColour(ink().withAlpha(0.82f));
        graphics.drawFittedText(juce::String(gridCount) + (gridCount == 1 ? " grid" : " grids")
                                    + "  " + juce::String(states[static_cast<std::size_t>(index)].bpm, 0) + " BPM",
                                bounds.withTrimmedTop(bounds.getHeight() * 0.54f).reduced(8.0f, 0.0f).toNearestInt(),
                                juce::Justification::centred,
                                1);

        if (selected && playing)
        {
            const auto liveDot = juce::Rectangle<float>(9.0f, 9.0f).withCentre({ bounds.getCentreX(), bounds.getY() - 7.0f });
            graphics.setColour(accent(index));
            graphics.fillEllipse(liveDot);
        }
    }

    graphics.restoreState();
}

void StateGraphComponent::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();

    if (event.mods.isRightButtonDown())
    {
        rightDragPanAnchor = event.position;
        return;
    }

    if (const auto index = stateAt(toCanvasPoint(event.position)); index >= 0)
    {
        if (onStateSelected)
            onStateSelected(index);
    }
}

void StateGraphComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (! rightDragPanAnchor.has_value() || ! event.mods.isRightButtonDown())
        return;

    const auto delta = event.position - *rightDragPanAnchor;
    panOffset += delta;
    rightDragPanAnchor = event.position;
    repaint();
}

void StateGraphComponent::mouseUp(const juce::MouseEvent&)
{
    rightDragPanAnchor.reset();
}

void StateGraphComponent::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto direction = wheel.deltaY == 0.0f ? -wheel.deltaX : wheel.deltaY;

    if (direction == 0.0f)
        return;

    zoomAt(zoom * std::pow(zoomStep, direction > 0.0f ? 1.0f : -1.0f), event.position);
}

void StateGraphComponent::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (stateAt(toCanvasPoint(event.position)) >= 0)
        return;

    if (onAddStateRequested)
        onAddStateRequested();
}

bool StateGraphComponent::keyPressed(const juce::KeyPress& key)
{
    const auto keyCode = key.getKeyCode();
    const auto shortcut = key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown();
    const auto character = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());

    if (keyCode == juce::KeyPress::deleteKey || keyCode == juce::KeyPress::backspaceKey)
    {
        if (onDeleteStateRequested)
            onDeleteStateRequested();

        return true;
    }

    if (shortcut && character == 'c')
    {
        if (onCopyStateRequested)
            onCopyStateRequested();

        return true;
    }

    if (shortcut && character == 'v')
    {
        if (onPasteStateRequested)
            onPasteStateRequested();

        return true;
    }

    return false;
}

std::vector<juce::Rectangle<float>> StateGraphComponent::getNodeBounds(juce::Rectangle<float> area) const
{
    std::vector<juce::Rectangle<float>> bounds;
    const auto count = static_cast<int>(states.size());

    if (count <= 0)
        return bounds;

    bounds.reserve(static_cast<std::size_t>(count));
    area = area.reduced(54.0f, 30.0f);
    const auto centre = area.getCentre();
    const auto growth = 1.0f + static_cast<float>(juce::jmax(0, count - 1)) * 0.075f;
    const auto radiusX = juce::jmax(1.0f, area.getWidth() * 0.34f * growth);
    const auto radiusY = juce::jmax(1.0f, area.getHeight() * 0.32f * growth);
    const auto densityTrim = static_cast<float>(juce::jmax(0, count - 8)) * 2.0f;
    const auto baseDiameter = juce::jlimit(56.0f, 118.0f, juce::jmin(area.getWidth(), area.getHeight()) * 0.38f - densityTrim);

    if (count == 1)
    {
        bounds.push_back(juce::Rectangle<float>(baseDiameter, baseDiameter).withCentre(centre));
        return bounds;
    }

    for (int index = 0; index < count; ++index)
    {
        const auto angle = -juce::MathConstants<float>::halfPi
                         + juce::MathConstants<float>::twoPi * static_cast<float>(index) / static_cast<float>(count);
        const auto diameter = baseDiameter + (index == selectedState ? 14.0f : 0.0f);
        bounds.push_back(juce::Rectangle<float>(diameter, diameter)
                             .withCentre(pointOnEllipse(centre, radiusX, radiusY, angle)));
    }

    return bounds;
}

juce::Rectangle<float> StateGraphComponent::getGraphBounds(juce::Rectangle<float> area) const
{
    const auto nodeBounds = getNodeBounds(area);

    if (nodeBounds.empty())
        return {};

    auto bounds = nodeBounds.front();

    for (const auto& node : nodeBounds)
        bounds = bounds.getUnion(node.expanded(44.0f));

    return bounds;
}

int StateGraphComponent::stateAt(const juce::Point<float> position) const
{
    const auto nodeBounds = getNodeBounds(getLocalBounds().toFloat().reduced(10.0f));

    for (int index = 0; index < static_cast<int>(nodeBounds.size()); ++index)
    {
        const auto bounds = nodeBounds[static_cast<std::size_t>(index)];

        const auto centre = bounds.getCentre();
        const auto radius = bounds.getWidth() * 0.5f;
        if (centre.getDistanceFrom(position) <= radius)
            return index;
    }

    return -1;
}

juce::Colour StateGraphComponent::colourForState(const int index) const
{
    return accent(index);
}

void StateGraphComponent::drawTransition(juce::Graphics& graphics,
                                         const TransitionView& transition,
                                         const std::vector<juce::Rectangle<float>>& nodeBounds,
                                         const int siblingIndex,
                                         const int siblingCount) const
{
    if (transition.fromState < 0 || transition.toState < 0
        || transition.fromState >= static_cast<int>(nodeBounds.size())
        || transition.toState >= static_cast<int>(nodeBounds.size()))
    {
        return;
    }

    const auto fromBounds = nodeBounds[static_cast<std::size_t>(transition.fromState)];
    const auto toBounds = nodeBounds[static_cast<std::size_t>(transition.toState)];
    const auto fromCentre = fromBounds.getCentre();
    const auto toCentre = toBounds.getCentre();
    const auto selectedEdge = transition.fromState == selectedState;
    const auto edgeColour = selectedEdge ? accent(transition.fromState) : ink();
    const auto alpha = selectedEdge ? 0.62f : 0.24f;

    graphics.setColour(edgeColour.withAlpha(alpha));

    if (transition.fromState == transition.toState)
    {
        const auto loop = fromBounds.expanded(20.0f).translated(fromBounds.getWidth() * 0.28f, -fromBounds.getHeight() * 0.18f);
        graphics.drawEllipse(loop, selectedEdge ? 1.4f : 0.9f);

        const auto arrowTip = juce::Point<float> { loop.getRight() - 6.0f, loop.getCentreY() + 12.0f };
        juce::Path arrow;
        arrow.startNewSubPath(arrowTip);
        arrow.lineTo(arrowTip.x - 10.0f, arrowTip.y - 3.0f);
        arrow.lineTo(arrowTip.x - 4.0f, arrowTip.y - 11.0f);
        arrow.closeSubPath();
        graphics.fillPath(arrow);

        if (transition.weighted)
        {
            const auto label = juce::String(juce::roundToInt(transition.probability * 100.0)) + "%";
            auto labelBounds = juce::Rectangle<float>(46.0f, 18.0f).withCentre({ loop.getCentreX(), loop.getY() });
            graphics.setColour(panel().withAlpha(0.90f));
            graphics.fillEllipse(labelBounds);
            graphics.setColour(ink().withAlpha(0.88f));
            graphics.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::bold));
            graphics.drawFittedText(label, labelBounds.toNearestInt(), juce::Justification::centred, 1);
        }

        return;
    }

    auto delta = toCentre - fromCentre;
    const auto distance = juce::jmax(1.0f, fromCentre.getDistanceFrom(toCentre));
    const auto unit = delta / distance;
    const auto normal = juce::Point<float> { -unit.y, unit.x };
    const auto fromRadius = fromBounds.getWidth() * 0.5f + 18.0f;
    const auto toRadius = toBounds.getWidth() * 0.5f + 18.0f;
    const auto start = fromCentre + unit * fromRadius;
    const auto end = toCentre - unit * toRadius;
    const auto siblingOffset = (static_cast<float>(siblingIndex) - static_cast<float>(siblingCount - 1) * 0.5f) * 18.0f;
    const auto curveOffset = normal * (22.0f + siblingOffset + (transition.weighted ? 10.0f : 0.0f));
    const auto control = (start + end) * 0.5f + curveOffset;

    juce::Path path;
    path.startNewSubPath(start);
    path.quadraticTo(control, end);
    graphics.strokePath(path, juce::PathStrokeType(selectedEdge ? 1.4f : 0.9f));

    auto tangent = end - control;
    const auto tangentLength = juce::jmax(1.0f, std::sqrt(tangent.x * tangent.x + tangent.y * tangent.y));
    tangent = tangent / tangentLength;
    const auto arrowNormal = juce::Point<float> { -tangent.y, tangent.x };
    const auto arrowTip = end;
    const auto arrowBack = arrowTip - tangent * 13.0f;

    juce::Path arrow;
    arrow.startNewSubPath(arrowTip);
    arrow.lineTo(arrowBack + arrowNormal * 5.0f);
    arrow.lineTo(arrowBack - arrowNormal * 5.0f);
    arrow.closeSubPath();
    graphics.fillPath(arrow);

    if (transition.weighted)
    {
        const auto label = juce::String(juce::roundToInt(transition.probability * 100.0)) + "%";
        auto labelBounds = juce::Rectangle<float>(48.0f, 18.0f).withCentre(control);
        graphics.setColour(panel().withAlpha(0.90f));
        graphics.fillEllipse(labelBounds);
        graphics.setColour(ink().withAlpha(0.72f));
        graphics.drawEllipse(labelBounds, 0.8f);
        graphics.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::bold));
        graphics.drawFittedText(label, labelBounds.toNearestInt(), juce::Justification::centred, 1);
    }
}

juce::Point<float> StateGraphComponent::toCanvasPoint(const juce::Point<float> position) const noexcept
{
    const auto area = getLocalBounds().toFloat().reduced(10.0f);
    const auto centre = area.getCentre();
    return { centre.x + (position.x - centre.x - panOffset.x) / zoom,
             centre.y + (position.y - centre.y - panOffset.y) / zoom };
}

void StateGraphComponent::zoomAt(const float newZoom, const juce::Point<float> anchor)
{
    const auto clampedZoom = juce::jlimit(minimumZoom, maximumZoom, newZoom);

    if (std::abs(clampedZoom - zoom) < 0.0001f)
        return;

    const auto before = toCanvasPoint(anchor);
    zoom = clampedZoom;
    const auto after = toCanvasPoint(anchor);
    panOffset += (after - before) * zoom;
    repaint();
}
}
