#include "MainComponent.h"

#include <cmath>
#include <regex>
#include <thread>

namespace gridcollider
{
namespace
{
struct ExtractedSynthDef
{
    juce::String name;
    juce::String source;
};

juce::String exampleMenuTitleFor(const juce::File& file)
{
    auto title = file.getFileNameWithoutExtension().replaceCharacter('-', ' ');

    for (int index = 0; index < title.length(); ++index)
    {
        if (index == 0 || title[index - 1] == ' ')
            title = title.replaceSection(index, 1, juce::String::charToString(juce::CharacterFunctions::toUpperCase(title[index])));
    }

    return title;
}

std::size_t findMatchingParen(const std::string& text, const std::size_t openIndex)
{
    int depth = 0;
    bool inString = false;
    bool inLineComment = false;
    bool inBlockComment = false;
    bool escaped = false;

    for (auto index = openIndex; index < text.size(); ++index)
    {
        const auto current = text[index];
        const auto next = index + 1 < text.size() ? text[index + 1] : '\0';

        if (inLineComment)
        {
            if (current == '\n' || current == '\r')
                inLineComment = false;

            continue;
        }

        if (inBlockComment)
        {
            if (current == '*' && next == '/')
            {
                inBlockComment = false;
                ++index;
            }

            continue;
        }

        if (inString)
        {
            if (escaped)
            {
                escaped = false;
                continue;
            }

            if (current == '\\')
            {
                escaped = true;
                continue;
            }

            if (current == '"')
                inString = false;

            continue;
        }

        if (current == '/' && next == '/')
        {
            inLineComment = true;
            ++index;
            continue;
        }

        if (current == '/' && next == '*')
        {
            inBlockComment = true;
            ++index;
            continue;
        }

        if (current == '"')
        {
            inString = true;
            continue;
        }

        if (current == '$')
        {
            ++index;
            continue;
        }

        if (current == '(')
        {
            ++depth;
            continue;
        }

        if (current == ')')
        {
            --depth;

            if (depth == 0)
                return index;
        }
    }

    return std::string::npos;
}

std::vector<ExtractedSynthDef> extractSynthDefs(const juce::String& source,
                                                const int stateNumber,
                                                const int laneNumber)
{
    std::vector<ExtractedSynthDef> synthDefs;
    juce::StringArray seen;
    const auto text = source.toStdString();
    const std::regex synthDefPattern(R"(SynthDef\s*\(\s*\\([A-Za-z_][A-Za-z0-9_]*))");

    for (auto iter = std::sregex_iterator(text.begin(), text.end(), synthDefPattern);
         iter != std::sregex_iterator();
         ++iter)
    {
        auto name = juce::String((*iter)[1].str()).trim();

        if (name.isEmpty() || seen.contains(name, true))
            continue;

        const auto start = static_cast<std::size_t>(iter->position(0));
        const auto open = text.find('(', start);

        if (open == std::string::npos)
            continue;

        const auto close = findMatchingParen(text, open);

        if (close == std::string::npos)
            continue;

        seen.add(name);
        synthDefs.push_back({ name, juce::String(text.substr(start, close + 1 - start)) });
    }

    if (synthDefs.empty())
        synthDefs.push_back({ "gc_s" + juce::String(stateNumber) + "_l" + juce::String(laneNumber), source });

    return synthDefs;
}
}

MainComponent::SourceCodeBackdropComponent::SourceCodeBackdropComponent()
{
    setInterceptsMouseClicks(false, false);
    setOpaque(false);
}

void MainComponent::SourceCodeBackdropComponent::setLanguage(const Language newLanguage)
{
    language = newLanguage;
    repaint();
}

void MainComponent::SourceCodeBackdropComponent::setSourceProvider(std::function<juce::String()> provider)
{
    sourceProvider = std::move(provider);
    repaint();
}

MainComponent::SourceCodeBackdropComponent::Kind MainComponent::SourceCodeBackdropComponent::classifyWord(const juce::String& word) const
{
    static const juce::StringArray superColliderKeywords {
        "arg", "var", "class", "this", "super", "nil", "true", "false", "inf",
        "if", "while", "for", "do", "case", "switch", "return", "break",
        "continue", "try", "catch", "protect", "new", "value", "play", "add",
        "kr", "ar", "ir"
    };
    static const juce::StringArray superColliderBuiltins {
        "SynthDef", "Synth", "Server", "Routine", "Task", "Pattern", "Pbind",
        "Pseq", "Prand", "Pwhite", "Pfunc", "Env", "EnvGen", "SinOsc",
        "Pulse", "Saw", "WhiteNoise", "Impulse", "Dust", "LFNoise0",
        "LFTri", "BPF", "RLPF", "LPF", "HPF", "FreeVerb", "Pan2", "Out"
    };
    static const juce::StringArray cppKeywords {
        "auto", "bool", "break", "case", "catch", "char", "class", "const",
        "constexpr", "continue", "double", "else", "enum", "false", "float",
        "for", "if", "int", "namespace", "private", "public", "return",
        "static", "struct", "switch", "true", "void", "while"
    };

    if (word.isEmpty())
        return Kind::normal;

    if (word.containsOnly("0123456789."))
        return Kind::number;

    if (language == Language::supercollider)
    {
        if (superColliderKeywords.contains(word))
            return Kind::keyword;
        if (superColliderBuiltins.contains(word))
            return Kind::builtin;
    }
    else if (cppKeywords.contains(word))
    {
        return Kind::keyword;
    }

    return Kind::normal;
}

juce::Colour MainComponent::SourceCodeBackdropComponent::colourForKind(const Kind kind, const float alpha) const
{
    switch (kind)
    {
        case Kind::comment:       return juce::Colour::fromRGB(118, 164, 142).withAlpha(alpha);
        case Kind::keyword:       return juce::Colour::fromRGB(224, 106, 42).withAlpha(alpha);
        case Kind::builtin:       return juce::Colour::fromRGB(96, 142, 196).withAlpha(alpha);
        case Kind::number:        return juce::Colour::fromRGB(205, 178, 70).withAlpha(alpha);
        case Kind::string:        return juce::Colour::fromRGB(111, 187, 112).withAlpha(alpha);
        case Kind::symbol:        return juce::Colour::fromRGB(202, 170, 92).withAlpha(alpha);
        case Kind::operatorToken: return juce::Colour::fromRGB(141, 120, 173).withAlpha(alpha);
        case Kind::bracket:       return juce::Colour::fromRGB(170, 176, 160).withAlpha(alpha);
        case Kind::normal:
        default:                  return juce::Colour::fromRGB(226, 230, 216).withAlpha(alpha);
    }
}

void MainComponent::SourceCodeBackdropComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour::fromRGB(18, 19, 18).withAlpha(0.92f));

    if (! sourceProvider)
        return;

    const auto text = sourceProvider();
    juce::StringArray lines;
    lines.addLines(text);

    const auto lineHeight = 17.0f;
    const auto charWidth = 8.2f;
    const auto maxLines = juce::jmin(lines.size(),
                                     juce::jmin(14, juce::jmax(1, static_cast<int>(std::ceil(static_cast<float>(getHeight()) / lineHeight)))));
    const auto left = 8.0f;
    graphics.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold));

    for (int row = 0; row < maxLines; ++row)
    {
        const auto line = lines[row];
        auto tokenStart = -1;
        auto tokenKind = Kind::normal;

        auto flushToken = [&](const int tokenEnd)
        {
            if (tokenStart < 0 || tokenEnd <= tokenStart)
                return;

            const auto token = line.substring(tokenStart, tokenEnd);
            const auto kind = tokenKind == Kind::normal ? classifyWord(token) : tokenKind;
            if (kind != Kind::normal)
            {
                const auto x = left + static_cast<float>(tokenStart) * charWidth;
                const auto y = 4.0f + static_cast<float>(row) * lineHeight;
                const auto w = juce::jmax(8.0f, static_cast<float>(tokenEnd - tokenStart) * charWidth);
                graphics.setColour(colourForKind(kind, 0.26f));
                graphics.fillRect(juce::Rectangle<float>(x, y + 1.0f, w, lineHeight - 3.0f));
            }

            tokenStart = -1;
            tokenKind = Kind::normal;
        };

        for (int column = 0; column <= line.length(); ++column)
        {
            const auto c = column < line.length() ? line[column] : ' ';
            const auto startsComment = column + 1 < line.length() && c == '/' && line[column + 1] == '/';
            const auto isQuote = c == '"' || c == '\'';
            const auto isBracket = c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}';
            const auto isOperator = juce::String("+-*/%=<>|&:;,.~").containsChar(c);
            const auto isWord = juce::CharacterFunctions::isLetterOrDigit(static_cast<juce::juce_wchar>(c)) || c == '_';

            if (startsComment)
            {
                flushToken(column);
                tokenStart = column;
                tokenKind = Kind::comment;
                flushToken(line.length());
                break;
            }

            if (isQuote || isBracket || isOperator)
            {
                flushToken(column);
                tokenStart = column;
                tokenKind = isQuote ? Kind::string : (isBracket ? Kind::bracket : Kind::operatorToken);
                flushToken(column + 1);
                continue;
            }

            if (isWord)
            {
                if (tokenStart < 0)
                    tokenStart = column;
                continue;
            }

            flushToken(column);
        }

        graphics.setColour(juce::Colour::fromRGB(226, 230, 216).withAlpha(0.08f));
        graphics.drawHorizontalLine(juce::roundToInt(4.0f + static_cast<float>(row + 1) * lineHeight),
                                    left,
                                    static_cast<float>(getWidth()) - 8.0f);
    }
}

namespace
{
bool isScIdentifierStart(const juce::juce_wchar c) noexcept
{
    return juce::CharacterFunctions::isLetter(c) || c == '_' || c == '~' || c == '\\';
}

bool isScIdentifierBody(const juce::juce_wchar c) noexcept
{
    return juce::CharacterFunctions::isLetterOrDigit(c) || c == '_' || c == '~' || c == '\\';
}

bool isScKeyword(const juce::String& token)
{
    static const juce::StringArray keywords {
        "arg", "var", "class", "this", "super", "nil", "true", "false", "inf",
        "if", "while", "for", "do", "case", "switch", "return", "break",
        "continue", "try", "catch", "protect", "new", "value", "play", "add",
        "kr", "ar", "ir"
    };

    return keywords.contains(token);
}

bool isScBuiltin(const juce::String& token)
{
    static const juce::StringArray builtins {
        "SynthDef", "Synth", "Server", "Routine", "Task", "Pattern", "Pbind",
        "Pseq", "Prand", "Pwhite", "Pfunc", "Env", "EnvGen", "SinOsc",
        "Pulse", "Saw", "WhiteNoise", "PinkNoise", "BrownNoise", "Impulse",
        "Dust", "LFNoise0", "LFNoise1", "LFNoise2", "LFTri", "LFSaw",
        "BPF", "RLPF", "LPF", "HPF", "MoogFF", "FreeVerb", "CombC",
        "DelayC", "Pan2", "Out", "Mix", "Splay", "Demand", "Dseq"
    };

    return builtins.contains(token);
}

void skipScQuotedString(juce::CodeDocument::Iterator& source, const juce::juce_wchar quote)
{
    source.skip();

    while (! source.isEOF())
    {
        const auto c = source.nextChar();

        if (c == '\\')
        {
            if (! source.isEOF())
                source.skip();
            continue;
        }

        if (c == quote)
            break;
    }
}
}

int MainComponent::SuperColliderCodeTokeniser::readNextToken(juce::CodeDocument::Iterator& source)
{
    source.skipWhitespace();

    const auto first = source.peekNextChar();

    if (first == 0)
        return tokenType_error;

    if (first == '/')
    {
        source.skip();

        if (source.peekNextChar() == '/')
        {
            source.skipToEndOfLine();
            return tokenType_comment;
        }

        return tokenType_operator;
    }

    if (first == '"' || first == '\'')
    {
        skipScQuotedString(source, first);
        return tokenType_string;
    }

    if (juce::CharacterFunctions::isDigit(first)
        || (first == '.' && juce::CharacterFunctions::isDigit(source.peekNextChar())))
    {
        bool seenDot = false;

        while (! source.isEOF())
        {
            const auto c = source.peekNextChar();
            if (c == '.' && ! seenDot)
            {
                seenDot = true;
                source.skip();
                continue;
            }

            if (! juce::CharacterFunctions::isDigit(c))
                break;

            source.skip();
        }

        return tokenType_number;
    }

    if (isScIdentifierStart(first))
    {
        juce::String token;

        while (! source.isEOF() && isScIdentifierBody(source.peekNextChar()))
            token += juce::String::charToString(source.nextChar());

        if (token.startsWithChar('\\') || token.startsWithChar('~'))
            return tokenType_symbol;

        if (isScKeyword(token))
            return tokenType_keyword;

        if (isScBuiltin(token))
            return tokenType_builtin;

        return tokenType_identifier;
    }

    if (first == '(' || first == ')' || first == '[' || first == ']' || first == '{' || first == '}')
    {
        source.skip();
        return tokenType_bracket;
    }

    if (juce::String("+-*%=<>!&|^:").containsChar(first))
    {
        source.skip();
        return tokenType_operator;
    }

    source.skip();
    return tokenType_punctuation;
}

juce::CodeEditorComponent::ColourScheme MainComponent::SuperColliderCodeTokeniser::getDefaultColourScheme()
{
    juce::CodeEditorComponent::ColourScheme scheme;
    scheme.set("Error", juce::Colour::fromRGB(255, 54, 46));
    scheme.set("Comment", juce::Colour::fromRGB(139, 148, 158));
    scheme.set("Keyword", juce::Colour::fromRGB(255, 159, 10));
    scheme.set("Builtin", juce::Colour::fromRGB(90, 170, 255));
    scheme.set("Identifier", juce::Colour::fromRGB(238, 239, 240));
    scheme.set("Number", juce::Colour::fromRGB(255, 214, 10));
    scheme.set("String", juce::Colour::fromRGB(68, 215, 120));
    scheme.set("Symbol", juce::Colour::fromRGB(191, 142, 255));
    scheme.set("Operator", juce::Colour::fromRGB(210, 214, 218));
    scheme.set("Bracket", juce::Colour::fromRGB(255, 105, 97));
    scheme.set("Punctuation", juce::Colour::fromRGB(184, 188, 192));
    return scheme;
}

void MainComponent::ArrangementContentComponent::setArrangement(std::vector<State> newStates,
                                                                std::vector<Edge> newEdges,
                                                                const int selectedIndex)
{
    states = std::move(newStates);
    edges = std::move(newEdges);
    selectedStateIndex = selectedIndex;
    repaint();
}

void MainComponent::ArrangementContentComponent::paint(juce::Graphics& graphics)
{
    const auto background = juce::Colour::fromRGB(13, 15, 18);
    const auto panel = juce::Colour::fromRGB(20, 23, 27);
    const auto line = juce::Colour::fromRGB(60, 66, 72);
    const auto ink = juce::Colour::fromRGB(236, 238, 232);
    const auto muted = juce::Colour::fromRGB(144, 150, 154);

    graphics.fillAll(background);

    auto bounds = getLocalBounds().reduced(18, 16);
    auto tabRow = bounds.removeFromTop(40);
    bounds.removeFromTop(14);

    graphics.setFont(juce::FontOptions(juce::Font::getDefaultSansSerifFontName(), 15.0f, juce::Font::bold));
    for (int index = 0; index < static_cast<int>(states.size()); ++index)
    {
        auto tab = tabRow.removeFromLeft(juce::jlimit(112, 178, (getWidth() - 60) / juce::jmax(1, static_cast<int>(states.size())))).reduced(3, 2);
        const auto& state = states[static_cast<std::size_t>(index)];
        graphics.setColour(state.selected ? state.colour.withAlpha(0.38f) : panel);
        graphics.fillRoundedRectangle(tab.toFloat(), 7.0f);
        graphics.setColour(state.selected ? state.colour : line);
        graphics.drawRoundedRectangle(tab.toFloat().reduced(0.5f), 7.0f, state.selected ? 2.0f : 1.0f);
        graphics.setColour(state.selected ? ink : muted);
        graphics.drawFittedText(state.name, tab.reduced(12, 0), juce::Justification::centred, 1);
    }

    auto titleRow = bounds.removeFromTop(34);
    int totalBars = 0;
    for (const auto& state : states)
        totalBars += juce::jmax(1, state.bars);

    graphics.setColour(ink);
    graphics.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    graphics.drawText("Arrangement", titleRow.removeFromLeft(160), juce::Justification::centredLeft);
    graphics.setColour(muted);
    graphics.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    graphics.drawText(juce::String(totalBars) + " bars   " + juce::String(states.size()) + " states",
                      titleRow,
                      juce::Justification::centredLeft);

    const auto timelineHeight = juce::jlimit(260, 420, bounds.getHeight() / 2);
    auto timeline = bounds.removeFromTop(timelineHeight).reduced(8, 0);
    auto ruler = timeline.removeFromTop(24);
    auto blockArea = timeline.removeFromTop(76);
    timeline.removeFromTop(20);
    auto arcArea = timeline.removeFromTop(juce::jmax(130, timeline.getHeight() - 18));

    if (states.empty())
        return;

    std::vector<juce::Rectangle<float>> blockBounds;
    blockBounds.reserve(states.size());
    const auto totalWeightedBars = juce::jmax(1, totalBars);
    auto x = static_cast<float>(blockArea.getX());
    const auto blockGap = 6.0f;
    const auto usableWidth = static_cast<float>(blockArea.getWidth()) - blockGap * static_cast<float>(juce::jmax(0, static_cast<int>(states.size()) - 1));

    graphics.setColour(line.withAlpha(0.65f));
    graphics.drawHorizontalLine(ruler.getCentreY(), static_cast<float>(ruler.getX()), static_cast<float>(ruler.getRight()));

    int barCursor = 1;
    for (int index = 0; index < static_cast<int>(states.size()); ++index)
    {
        const auto& state = states[static_cast<std::size_t>(index)];
        const auto bars = juce::jmax(1, state.bars);
        const auto width = juce::jmax(58.0f, usableWidth * static_cast<float>(bars) / static_cast<float>(totalWeightedBars));
        auto block = juce::Rectangle<float>(x, static_cast<float>(blockArea.getY()), width, static_cast<float>(blockArea.getHeight()));
        blockBounds.push_back(block);

        graphics.setColour(muted.withAlpha(0.7f));
        graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        graphics.drawText(juce::String(barCursor), juce::Rectangle<float>(block.getX() + 4.0f, static_cast<float>(ruler.getY()), 48.0f, static_cast<float>(ruler.getHeight())).toNearestInt(), juce::Justification::centredLeft);
        barCursor += bars;

        graphics.setColour(state.colour.withAlpha(state.selected ? 0.44f : 0.24f));
        graphics.fillRoundedRectangle(block, 6.0f);
        graphics.setColour(state.selected ? juce::Colours::white : state.colour.withAlpha(0.78f));
        graphics.drawRoundedRectangle(block.reduced(0.5f), 6.0f, state.selected ? 2.0f : 1.0f);

        auto text = block.toNearestInt().reduced(12, 8);
        graphics.setColour(ink);
        graphics.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        graphics.drawFittedText(state.name, text.removeFromTop(24), juce::Justification::centredLeft, 1);
        graphics.setColour(muted);
        graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        graphics.drawFittedText(juce::String(bars) + " bars   " + juce::String(state.laneCount) + " lanes   "
                                    + juce::String(state.numerator) + "/" + juce::String(state.denominator),
                                text,
                                juce::Justification::centredLeft,
                                1);
        x += width + blockGap;
    }

    for (const auto& edge : edges)
    {
        if (edge.from < 0 || edge.to < 0 || edge.from >= static_cast<int>(blockBounds.size()) || edge.to >= static_cast<int>(blockBounds.size()))
            continue;

        const auto from = blockBounds[static_cast<std::size_t>(edge.from)].getBottomLeft()
                            + juce::Point<float>(blockBounds[static_cast<std::size_t>(edge.from)].getWidth() * 0.5f, 18.0f);
        const auto to = blockBounds[static_cast<std::size_t>(edge.to)].getBottomLeft()
                            + juce::Point<float>(blockBounds[static_cast<std::size_t>(edge.to)].getWidth() * 0.5f, 18.0f);
        const auto colour = states[static_cast<std::size_t>(edge.from)].colour.withAlpha(edge.weighted ? 0.52f : 0.34f);
        juce::Path path;
        path.startNewSubPath(from);
        const auto span = static_cast<float>(std::abs(edge.to - edge.from));
        const auto hang = juce::jlimit(38.0f,
                                       static_cast<float>(arcArea.getHeight()) - 18.0f,
                                       48.0f + span * 28.0f);
        const auto controlY = static_cast<float>(arcArea.getY()) + hang;
        path.cubicTo(from.x, controlY, to.x, controlY, to.x, to.y);
        graphics.setColour(colour);
        graphics.strokePath(path, juce::PathStrokeType(edge.weighted ? 2.0f : 1.2f));

        if (edge.weighted)
        {
            const auto label = juce::String(juce::roundToInt(edge.chance * 100.0)) + "%";
            graphics.setColour(panel.withAlpha(0.92f));
            const auto labelBounds = juce::Rectangle<float>((from.x + to.x) * 0.5f - 18.0f,
                                                            controlY - 9.0f,
                                                            36.0f,
                                                            18.0f);
            graphics.fillRoundedRectangle(labelBounds, 8.0f);
            graphics.setColour(ink);
            graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
            graphics.drawFittedText(label, labelBounds.toNearestInt(), juce::Justification::centred, 1);
        }
    }

    auto laneArea = bounds.reduced(8, 18);
    graphics.setColour(line.withAlpha(0.35f));
    graphics.drawHorizontalLine(laneArea.getY(), static_cast<float>(laneArea.getX()), static_cast<float>(laneArea.getRight()));
    laneArea.removeFromTop(26);

    for (int index = 0; index < static_cast<int>(states.size()) && index < static_cast<int>(blockBounds.size()); ++index)
    {
        const auto& state = states[static_cast<std::size_t>(index)];
        auto mini = juce::Rectangle<float>(blockBounds[static_cast<std::size_t>(index)].getX(),
                                           static_cast<float>(laneArea.getY()),
                                           blockBounds[static_cast<std::size_t>(index)].getWidth(),
                                           62.0f);
        const auto laneWidth = juce::jmin(28.0f, (mini.getWidth() - 8.0f) / static_cast<float>(juce::jmax(1, state.laneCount)));
        auto laneX = mini.getX();
        for (int lane = 0; lane < state.laneCount; ++lane)
        {
            const auto colour = state.laneColours.empty()
                                    ? state.colour
                                    : state.laneColours[static_cast<std::size_t>(lane) % state.laneColours.size()];
            auto laneBlock = juce::Rectangle<float>(laneX, mini.getY() + 18.0f, laneWidth, 34.0f).reduced(2.0f, 0.0f);
            graphics.setColour(colour.withAlpha(0.72f));
            graphics.fillRoundedRectangle(laneBlock, 4.0f);
            graphics.setColour(colour);
            graphics.drawRoundedRectangle(laneBlock.reduced(0.5f), 4.0f, 1.0f);
            laneX += laneWidth;
        }
    }
}

namespace
{
constexpr int maximumCompositionStates = 16;
constexpr int maximumGridsPerState = 8;
constexpr int outerMargin = 22;
constexpr int headerHeight = 96;
constexpr int headerToTransitionGap = 10;
constexpr int transitionSplitterThickness = 10;
constexpr int gridSplitterThickness = 12;
constexpr int minimumTransitionPaneHeight = 44;
constexpr int minimumLowerPaneHeight = 240;
constexpr int minimumSplitPaneWidth = 180;
constexpr int maximumGridColumns = 64;
constexpr int maximumGridRows = 32;
constexpr double minimumGridTempoRatio = 1.0;
constexpr double maximumGridTempoRatio = 16.0;
constexpr float panelRadius = 5.0f;
constexpr float innerPanelRadius = 3.0f;

[[nodiscard]] juce::Colour lewittPaper() noexcept { return juce::Colour::fromRGB(41, 42, 43); }
[[nodiscard]] juce::Colour lewittPanel() noexcept { return juce::Colour::fromRGB(55, 56, 57); }
[[nodiscard]] juce::Colour lewittInk() noexcept { return juce::Colour::fromRGB(238, 239, 240); }
[[nodiscard]] juce::Colour lewittLine() noexcept { return juce::Colour::fromRGB(116, 119, 122); }
[[nodiscard]] juce::Colour lewittBlue() noexcept { return juce::Colour::fromRGB(0, 122, 255); }
[[nodiscard]] juce::Colour lewittGreen() noexcept { return juce::Colour::fromRGB(50, 215, 75); }

void forceBlackEditorText(juce::TextEditor& editor)
{
    editor.setColour(juce::TextEditor::textColourId, lewittInk());
    editor.setColour(juce::TextEditor::highlightedTextColourId, lewittInk());
    editor.setColour(juce::CaretComponent::caretColourId, lewittInk());
    editor.setColour(juce::Label::textColourId, lewittInk());
    editor.setColour(juce::Label::textWhenEditingColourId, lewittInk());
    editor.applyColourToAllText(lewittInk(), true);
}

[[nodiscard]] GridModel::Snapshot resizeSnapshot(GridModel::Snapshot source, const int columns, const int rows)
{
    const auto width = juce::jlimit(1, maximumGridColumns, columns);
    const auto height = juce::jlimit(1, maximumGridRows, rows);
    GridModel::Snapshot resized;
    resized.width = width;
    resized.height = height;
    resized.cells.assign(static_cast<std::size_t>(width * height), GridModel::emptyGlyph);

    for (int row = 0; row < juce::jmin(height, source.height); ++row)
        for (int column = 0; column < juce::jmin(width, source.width); ++column)
            resized.cells[static_cast<std::size_t>(column + row * width)] = source.getGlyph(column, row);

    return resized;
}
}

MainComponent::MainComponent()
    : gridEditor(gridModel),
      transitionCodeEditor(transitionCodeDocument, &scCodeTokeniser),
      laneScCodeEditor(laneScCodeDocument, &scCodeTokeniser),
      instrumentCodeEditor(instrumentCodeDocument, &scCodeTokeniser)
{
    setLookAndFeel(&minimalLookAndFeel);
    initialiseDefaultInstrumentLayer();

    CompositionGrid firstGrid;
    firstGrid.snapshot = gridModel.createSnapshot();

    CompositionState firstState;
    firstState.name = "State 01";
    firstState.transitionCode = createDefaultTransitionCode(1);
    firstState.grids.push_back(std::move(firstGrid));
    compositionStates.push_back(std::move(firstState));

    addAndMakeVisible(stateGraph);
    addAndMakeVisible(gridEditor);
    stateGraph.addKeyListener(this);
    gridEditor.addKeyListener(this);

    GridEditorComponent::Theme gridTheme;
    gridTheme.background = juce::Colour::fromRGB(30, 31, 32);
    gridTheme.viewportBackground = juce::Colour::fromRGB(20, 21, 22);
    gridTheme.gridLine = juce::Colour::fromRGB(65, 68, 72);
    gridTheme.text = lewittInk();
    gridTheme.mutedText = lewittInk().withAlpha(0.42f);
    gridTheme.rulerBackground = juce::Colour::fromRGB(48, 50, 52);
    gridTheme.rulerText = lewittInk().withAlpha(0.72f);
    gridTheme.cursor = lewittBlue();
    gridTheme.cursorText = lewittInk();
    gridTheme.selection = lewittBlue().withAlpha(0.28f);
    gridTheme.selectionBorder = lewittBlue().brighter(0.22f);
    gridTheme.playhead = lewittBlue().withAlpha(0.18f);
    gridEditor.setTheme(gridTheme);

    configureOscControls();
    configureExampleControls();
    configurePatternControls();
    configureTransitionCodePane();
    configureEventMonitor();
    configureTransportControls();
    configureStateGraph();
    configureGridSlotControls();
    configureLaneCodePane();
    configureMixerView();
    configureArrangementView();
    configureInstrumentView();
    statusLog.append("GridCollider ready");
    statusLog.append("OSC target: " + oscOutput.getEndpointDescription());

    configureTransport();
    setAudioChannels(0, 2);
    startTimerHz(60);

    for (auto& meter : mixerMeterPeaks)
        meter.store(0.0f, std::memory_order_relaxed);
    mixerMeterDisplay.fill(0.0f);

    setOpaque(true);
    setSize(1280, 900);
}

MainComponent::~MainComponent()
{
    stopTimer();
    transitionCodeDocument.removeListener(&transitionCodeDocumentListener);
    laneScCodeDocument.removeListener(&laneScCodeDocumentListener);
    instrumentCodeDocument.removeListener(&instrumentCodeDocumentListener);
    setLookAndFeel(nullptr);
    transportEngine.stop();
    stateGraph.removeKeyListener(this);
    gridEditor.removeKeyListener(this);
    shutdownAudio();
}

void MainComponent::menuLoadComposition()
{
    showLoadCompositionDialog();
}

void MainComponent::menuSaveComposition()
{
    if (currentCompositionFile == juce::File())
    {
        showSaveCompositionDialog();
        return;
    }

    saveCompositionFile(currentCompositionFile);
}

void MainComponent::menuSaveCompositionAs()
{
    showSaveCompositionDialog();
}

void MainComponent::menuExportStereoWav()
{
    if (exportCaptureActive.load(std::memory_order_acquire))
    {
        statusLog.append("WAV export already running");
        repaint();
        return;
    }

    auto* prompt = new juce::AlertWindow("Export Stereo WAV",
                                         "Enter the duration to render, in seconds.",
                                         juce::MessageBoxIconType::NoIcon);
    prompt->addTextEditor("duration", "120", "Duration");
    prompt->addButton("Export", 1, juce::KeyPress(juce::KeyPress::returnKey));
    prompt->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    prompt->enterModalState(true,
                            juce::ModalCallbackFunction::create([safeThis = juce::Component::SafePointer<MainComponent>(this), prompt](const int result)
                            {
                                if (safeThis == nullptr || result != 1)
                                    return;

                                const auto durationSeconds = juce::jlimit(1.0,
                                                                          3600.0,
                                                                          prompt->getTextEditorContents("duration").getDoubleValue());
                                safeThis->showExportWavDialog(durationSeconds > 0.0 ? durationSeconds : 120.0);
                            }),
                            true);
}

void MainComponent::menuShowMainView()
{
    mixerViewVisible = false;
    arrangementViewVisible = false;
    instrumentsViewVisible = false;
    activeSplitterDrag = SplitterDrag::none;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    showActiveLane();
    resized();
    repaint();
}

void MainComponent::menuToggleMixerView()
{
    mixerViewVisible = true;
    arrangementViewVisible = false;
    instrumentsViewVisible = false;
    activeSplitterDrag = SplitterDrag::none;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    refreshMixerView();
    resized();
    repaint();
}

void MainComponent::menuToggleArrangementView()
{
    arrangementViewVisible = true;
    mixerViewVisible = false;
    instrumentsViewVisible = false;
    activeSplitterDrag = SplitterDrag::none;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    refreshArrangementView();
    resized();
    repaint();
}

void MainComponent::menuToggleInstrumentsView()
{
    instrumentsViewVisible = true;
    mixerViewVisible = false;
    arrangementViewVisible = false;
    activeSplitterDrag = SplitterDrag::none;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    refreshInstrumentView();
    resized();
    repaint();
}

void MainComponent::menuLoadExample(const juce::File& file)
{
    loadExampleFile(file);
}

bool MainComponent::isMixerViewVisible() const noexcept
{
    return mixerViewVisible;
}

bool MainComponent::isArrangementViewVisible() const noexcept
{
    return arrangementViewVisible;
}

bool MainComponent::isInstrumentsViewVisible() const noexcept
{
    return instrumentsViewVisible;
}

MainComponent::MainLayout MainComponent::calculateMainLayout() const
{
    MainLayout layout;
    auto bounds = getLocalBounds().reduced(outerMargin);

    layout.header = bounds.removeFromTop(headerHeight);
    bounds.removeFromTop(headerToTransitionGap);

    const auto maximumTransitionHeight = juce::jmax(minimumTransitionPaneHeight,
                                                    bounds.getHeight() - transitionSplitterThickness - minimumLowerPaneHeight);
    const auto transitionHeight = juce::jlimit(minimumTransitionPaneHeight,
                                               maximumTransitionHeight,
                                               transitionPaneHeight);

    layout.transitionPane = bounds.removeFromTop(transitionHeight);
    layout.transitionSplitter = bounds.removeFromTop(transitionSplitterThickness);

    const auto lowerWidth = bounds.getWidth();
    const auto maxStateWidth = juce::jmax(minimumSplitPaneWidth,
                                          lowerWidth - gridSplitterThickness - minimumSplitPaneWidth);
    const auto requestedStateWidth = juce::roundToInt(static_cast<double>(lowerWidth - gridSplitterThickness) * fsmGridSplitRatio);
    const auto stateWidth = juce::jlimit(minimumSplitPaneWidth, maxStateWidth, requestedStateWidth);

    layout.statePane = bounds.removeFromLeft(stateWidth);
    layout.gridSplitter = bounds.removeFromLeft(gridSplitterThickness);
    layout.gridPane = bounds;

    return layout;
}

MainComponent::SplitterDrag MainComponent::splitterAt(const juce::Point<int> position) const
{
    const auto layout = calculateMainLayout();
    const auto optionalWorkspaceVisible = mixerViewVisible || arrangementViewVisible || instrumentsViewVisible;

    if (optionalWorkspaceVisible)
        return SplitterDrag::none;

    if (layout.transitionSplitter.expanded(0, 3).contains(position))
        return SplitterDrag::transition;

    if (layout.gridSplitter.expanded(3, 0).contains(position))
        return SplitterDrag::grid;

    return SplitterDrag::none;
}

void MainComponent::updateSplitterCursor(const juce::Point<int> position)
{
    switch (splitterAt(position))
    {
        case SplitterDrag::transition:
            setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
            break;
        case SplitterDrag::grid:
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            break;
        case SplitterDrag::none:
            setMouseCursor(juce::MouseCursor::NormalCursor);
            break;
    }
}

void MainComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(lewittPaper());

    const auto layout = calculateMainLayout();
    auto header = layout.header;
    const auto optionalWorkspaceVisible = mixerViewVisible || arrangementViewVisible || instrumentsViewVisible;

    graphics.setColour(lewittPanel());
    graphics.fillRoundedRectangle(layout.header.toFloat(), panelRadius);
    if (! optionalWorkspaceVisible)
    {
        graphics.setColour(juce::Colour::fromRGB(35, 36, 37));
        graphics.fillRoundedRectangle(layout.transitionPane.toFloat(), panelRadius);
    }

    graphics.setColour(lewittLine().withAlpha(0.55f));
    graphics.drawRoundedRectangle(layout.header.toFloat().reduced(0.5f), panelRadius, 1.0f);
    if (! optionalWorkspaceVisible)
        graphics.drawRoundedRectangle(layout.transitionPane.toFloat().reduced(0.5f), panelRadius, 1.0f);

    auto lowerWorkspace = getLocalBounds().reduced(outerMargin);
    lowerWorkspace.removeFromTop(optionalWorkspaceVisible
                                     ? headerHeight + headerToTransitionGap
                                     : headerHeight + headerToTransitionGap + layout.transitionPane.getHeight() + transitionSplitterThickness);

    if (optionalWorkspaceVisible)
    {
        auto optionalArea = lowerWorkspace;

        graphics.setColour(juce::Colour::fromRGB(35, 36, 37));
        graphics.fillRoundedRectangle(optionalArea.toFloat(), panelRadius);
        graphics.setColour(lewittLine().withAlpha(0.55f));
        graphics.drawRoundedRectangle(optionalArea.toFloat().reduced(0.5f), panelRadius, 1.0f);
    }
    else
    {
        graphics.drawRoundedRectangle(layout.statePane.toFloat().reduced(0.5f), innerPanelRadius, 1.0f);
        graphics.drawRoundedRectangle(layout.gridPane.toFloat().reduced(0.5f), innerPanelRadius, 1.0f);

        graphics.setColour(lewittLine().withAlpha(activeSplitterDrag == SplitterDrag::grid ? 1.0f : 0.55f));
        graphics.drawVerticalLine(layout.gridSplitter.getCentreX(),
                                  static_cast<float>(layout.gridSplitter.getY()),
                                  static_cast<float>(layout.gridSplitter.getBottom()));

        const auto gridHandle = layout.gridSplitter.withSizeKeepingCentre(6, 96);
        graphics.drawRoundedRectangle(gridHandle.toFloat().reduced(0.5f), 2.0f, 1.0f);
    }

    if (! optionalWorkspaceVisible)
    {
        graphics.setColour(lewittLine().withAlpha(activeSplitterDrag == SplitterDrag::transition ? 1.0f : 0.55f));
        graphics.drawHorizontalLine(layout.transitionSplitter.getCentreY(),
                                    static_cast<float>(layout.transitionSplitter.getX()),
                                    static_cast<float>(layout.transitionSplitter.getRight()));

        const auto transitionHandle = layout.transitionSplitter.withSizeKeepingCentre(96, 6);
        graphics.drawRoundedRectangle(transitionHandle.toFloat().reduced(0.5f), 2.0f, 1.0f);
    }

    auto headerGridArea = header.reduced(16, 10);
    const auto titleColumnWidth = juce::jlimit(190, 252, header.getWidth() / 7);
    auto titleArea = headerGridArea.removeFromLeft(titleColumnWidth);
    const int toolbarColumns = 20;
    const int toolbarRows = 2;
    const int toolbarGap = 8;
    const auto squareSize = juce::jmax(20,
                                       juce::jmin((headerGridArea.getHeight() - toolbarGap) / toolbarRows,
                                                  (headerGridArea.getWidth() - (toolbarColumns - 1) * toolbarGap) / toolbarColumns));
    const auto toolbarWidth = toolbarColumns * squareSize + (toolbarColumns - 1) * toolbarGap;
    const auto toolbarHeight = toolbarRows * squareSize + toolbarGap;
    const auto toolbarArea = juce::Rectangle<int>(headerGridArea.getX() + juce::jmax(0, (headerGridArea.getWidth() - toolbarWidth) / 2),
                                                  headerGridArea.getCentreY() - toolbarHeight / 2,
                                                  toolbarWidth,
                                                  toolbarHeight);

    graphics.setColour(lewittLine().withAlpha(0.24f));
    for (int row = 0; row < toolbarRows; ++row)
    {
        for (int column = 0; column < toolbarColumns; ++column)
        {
            const auto cell = juce::Rectangle<int>(toolbarArea.getX() + column * (squareSize + toolbarGap),
                                                   toolbarArea.getY() + row * (squareSize + toolbarGap),
                                                   squareSize,
                                                   squareSize);
            graphics.drawRoundedRectangle(cell.toFloat().reduced(0.5f), 4.0f, 1.0f);
        }
    }

    graphics.setColour(lewittInk());
    graphics.setFont(juce::FontOptions(23.0f, juce::Font::bold));
    graphics.drawText("GridCollider", titleArea, juce::Justification::centredLeft);

    const auto pulseCell = juce::Rectangle<int>(toolbarArea.getX() + 10 * (squareSize + toolbarGap),
                                                toolbarArea.getY(),
                                                squareSize,
                                                squareSize).toFloat();
    const auto elapsed = juce::Time::getMillisecondCounterHiRes() - lastPulseTimeMs;
    const auto pulseAlpha = transportEngine.isPlaying()
                                ? juce::jlimit(0.25f, 1.0f, 1.0f - static_cast<float>(elapsed / 180.0))
                                : 0.18f;

    graphics.setColour((transportEngine.isPlaying() ? lewittGreen() : lewittInk()).withAlpha(pulseAlpha));
    graphics.fillEllipse(pulseCell.withSizeKeepingCentre(8.0f, 8.0f));

    if (! optionalWorkspaceVisible)
    {
        auto transitionHeader = layout.transitionPane.reduced(10, 0).removeFromTop(26);
        graphics.setColour(lewittLine().withAlpha(0.36f));
        graphics.drawHorizontalLine(transitionHeader.getBottom(), static_cast<float>(transitionHeader.getX()), static_cast<float>(transitionHeader.getRight()));
    }

}

void MainComponent::resized()
{
    const auto layout = calculateMainLayout();
    auto header = layout.header;
    auto transitionArea = layout.transitionPane;
    const auto optionalWorkspaceVisible = mixerViewVisible || arrangementViewVisible || instrumentsViewVisible;

    auto lowerWorkspace = getLocalBounds().reduced(outerMargin);
    lowerWorkspace.removeFromTop(optionalWorkspaceVisible
                                     ? headerHeight + headerToTransitionGap
                                     : headerHeight + headerToTransitionGap + layout.transitionPane.getHeight() + transitionSplitterThickness);

    if (mixerViewVisible)
    {
        auto mixerArea = lowerWorkspace.reduced(0, 0);

        instrumentView.setVisible(false);
        instrumentCodeEditor.setVisible(false);
        stateGraph.setVisible(false);
        transitionCodeLabel.setVisible(false);
        transitionCodeBackdrop.setVisible(false);
        transitionCodeEditor.setVisible(false);
        arrangementViewport.setVisible(false);
        mixerViewport.setBounds(mixerArea);
        mixerViewport.setVisible(true);
        mixerViewport.toFront(false);
        refreshMixerView();

        gridEditor.setVisible(false);
        laneCodeBackdrop.setVisible(false);
        laneScCodeEditor.setVisible(false);
        for (auto& button : gridTabButtons)
            button.setVisible(false);
    }
    else if (arrangementViewVisible)
    {
        auto arrangementArea = lowerWorkspace.reduced(0, 0);

        instrumentView.setVisible(false);
        instrumentCodeEditor.setVisible(false);
        stateGraph.setVisible(false);
        transitionCodeLabel.setVisible(false);
        transitionCodeBackdrop.setVisible(false);
        transitionCodeEditor.setVisible(false);
        mixerViewport.setVisible(false);
        arrangementViewport.setBounds(arrangementArea);
        arrangementViewport.setVisible(true);
        arrangementViewport.toFront(false);
        refreshArrangementView();

        gridEditor.setVisible(false);
        laneCodeBackdrop.setVisible(false);
        laneScCodeEditor.setVisible(false);
        for (auto& button : gridTabButtons)
            button.setVisible(false);
    }
    else if (instrumentsViewVisible)
    {
        auto instrumentArea = lowerWorkspace.reduced(0, 0);

        stateGraph.setVisible(false);
        transitionCodeLabel.setVisible(false);
        transitionCodeBackdrop.setVisible(false);
        transitionCodeEditor.setVisible(false);
        mixerViewport.setVisible(false);
        arrangementViewport.setVisible(false);
        gridEditor.setVisible(false);
        laneCodeBackdrop.setVisible(false);
        laneScCodeEditor.setVisible(false);
        for (auto& button : gridTabButtons)
            button.setVisible(false);

        instrumentView.setBounds(instrumentArea);
        instrumentView.setVisible(true);
        instrumentCodeEditor.setVisible(true);
        instrumentView.toFront(false);
        instrumentCodeEditor.toFront(false);

        auto area = instrumentView.getLocalBounds().reduced(24);
        instrumentViewTitleLabel.setBounds(area.removeFromTop(28));
        area.removeFromTop(12);

        auto topControls = area.removeFromTop(36);
        instrumentSelector.setBounds(topControls.removeFromLeft(230));
        topControls.removeFromLeft(10);
        instrumentNameEditor.setBounds(topControls.removeFromLeft(220));
        topControls.removeFromLeft(10);
        newInstrumentButton.setBounds(topControls.removeFromLeft(90));
        topControls.removeFromLeft(8);
        deleteInstrumentButton.setBounds(topControls.removeFromLeft(90));
        topControls.removeFromLeft(8);
        saveInstrumentButton.setBounds(topControls.removeFromLeft(90));
        topControls.removeFromLeft(8);
        compileInstrumentButton.setBounds(topControls.removeFromLeft(110));

        area.removeFromTop(18);
        auto columns = area;
        auto mapArea = columns.removeFromLeft(360);
        columns.removeFromLeft(22);
        auto codeArea = columns;

        instrumentMapLabel.setBounds(mapArea.removeFromTop(22));
        mapArea.removeFromTop(8);
        const auto rowHeight = 28;
        const auto rowGap = 5;
        auto applyButtonArea = mapArea.removeFromBottom(34);
        mapArea.removeFromBottom(10);
        instrumentMapViewport.setBounds(mapArea);
        instrumentMapContent.setBounds(0,
                                       0,
                                       juce::jmax(260, mapArea.getWidth() - instrumentMapViewport.getScrollBarThickness()),
                                       instrumentChannelCount * (rowHeight + rowGap));
        auto contentArea = instrumentMapContent.getLocalBounds().reduced(0, 0);

        for (int channel = 0; channel < instrumentChannelCount; ++channel)
        {
            auto row = contentArea.removeFromTop(rowHeight);
            instrumentChannelLabels[static_cast<std::size_t>(channel)].setBounds(row.removeFromLeft(58));
            row.removeFromLeft(8);
            instrumentChannelSelectors[static_cast<std::size_t>(channel)].setBounds(row.removeFromLeft(180));
            contentArea.removeFromTop(rowGap);
        }

        applyInstrumentMapButton.setBounds(applyButtonArea.removeFromLeft(180));
        instrumentCodeLabel.setBounds(codeArea.removeFromTop(22));
        codeArea.removeFromTop(8);
        instrumentCodeEditor.setBounds(codeArea);
    }
    else
    {
        instrumentView.setVisible(false);
        instrumentCodeEditor.setVisible(false);
        stateGraph.setVisible(true);
        stateGraph.setBounds(layout.statePane);
        stateGraph.fitToView();
        transitionCodeLabel.setVisible(true);
        transitionCodeBackdrop.setVisible(false);
        transitionCodeEditor.setVisible(true);
        mixerViewport.setVisible(false);
        arrangementViewport.setVisible(false);
    }

    auto gridArea = layout.gridPane.reduced(12, 10);
    auto gridTabs = gridArea.removeFromTop(28);
    gridArea.removeFromTop(8);
    gridEditor.setBounds(gridArea);
    laneScCodeEditor.setBounds(gridArea.reduced(3));
    if (! optionalWorkspaceVisible)
        gridEditor.fitToView();

    int visibleTabs = 0;
    for (const auto& button : gridTabButtons)
        if (button.isVisible())
            ++visibleTabs;

    if (! optionalWorkspaceVisible && visibleTabs > 0)
    {
        const auto tabWidth = juce::jlimit(52, 94, (gridTabs.getWidth() - (visibleTabs - 1) * 6) / visibleTabs);

        for (auto& button : gridTabButtons)
        {
            if (! button.isVisible())
            {
                button.setBounds({});
                continue;
            }

            button.setBounds(gridTabs.removeFromLeft(tabWidth).withSizeKeepingCentre(tabWidth, 24));
            gridTabs.removeFromLeft(6);
        }
    }

    auto headerGridArea = header.reduced(16, 10);
    const auto titleColumnWidth = juce::jlimit(190, 252, header.getWidth() / 7);
    headerGridArea.removeFromLeft(titleColumnWidth);
    const int toolbarColumns = 20;
    const int toolbarRows = 2;
    const int toolbarGap = 8;
    const auto squareSize = juce::jmax(20,
                                       juce::jmin((headerGridArea.getHeight() - toolbarGap) / toolbarRows,
                                                  (headerGridArea.getWidth() - (toolbarColumns - 1) * toolbarGap) / toolbarColumns));
    const auto toolbarWidth = toolbarColumns * squareSize + (toolbarColumns - 1) * toolbarGap;
    const auto toolbarHeight = toolbarRows * squareSize + toolbarGap;
    const auto toolbarArea = juce::Rectangle<int>(headerGridArea.getX() + juce::jmax(0, (headerGridArea.getWidth() - toolbarWidth) / 2),
                                                  headerGridArea.getCentreY() - toolbarHeight / 2,
                                                  toolbarWidth,
                                                  toolbarHeight);
    const auto cell = [toolbarArea, squareSize](const int column, const int row, const int span = 1)
    {
        return juce::Rectangle<int>(toolbarArea.getX() + column * (squareSize + toolbarGap),
                                    toolbarArea.getY() + row * (squareSize + toolbarGap),
                                    span * squareSize + (span - 1) * toolbarGap,
                                    squareSize);
    };

    playPauseButton.setBounds(cell(0, 0, 2));
    stopButton.setBounds(cell(2, 0, 2));
    resetButton.setBounds(cell(4, 0, 2));
    bpmEditor.setBounds(cell(6, 0, 2));
    stateTimeSignatureNumeratorEditor.setBounds(cell(8, 0));
    stateTimeSignatureDenominatorEditor.setBounds(cell(9, 0));
    bpmLabel.setBounds({});
    stateTimeSignatureLabel.setBounds({});
    stateTimeSignatureSeparatorLabel.setBounds({});

    stateSlotLabel.setBounds(cell(0, 1));
    stateAdvanceLabel.setBounds(cell(1, 1));
    previousStateButton.setBounds(cell(2, 1));
    nextStateButton.setBounds(cell(3, 1));
    addStateButton.setBounds(cell(4, 1));
    stateAdvanceModeButton.setBounds(cell(5, 1, 2));
    stateAdvanceIntervalEditor.setBounds(cell(7, 1));
    gridSlotLabel.setBounds(cell(8, 1));
    gridRatioLabel.setBounds(cell(9, 1));
    previousGridButton.setBounds(cell(10, 1));
    nextGridButton.setBounds(cell(11, 1));
    addGridButton.setBounds(cell(12, 1));
    gridColumnsEditor.setBounds(cell(13, 1));
    gridRowsEditor.setBounds(cell(14, 1));
    laneKindButton.setBounds(cell(15, 1));
    phaseModeButton.setBounds(cell(16, 1, 2));
    phaseOffsetEditor.setBounds(cell(18, 1));
    gridRatioEditor.setBounds(cell(19, 1));
    gridSizeLabel.setBounds({});
    gridSizeSeparatorLabel.setBounds({});

    for (auto* component : { static_cast<juce::Component*>(&loadPatternButton),
                             static_cast<juce::Component*>(&savePatternButton),
                             static_cast<juce::Component*>(&recentPatternButton),
                             static_cast<juce::Component*>(&loadExampleButton),
                             static_cast<juce::Component*>(&embeddedScTestButton),
                             static_cast<juce::Component*>(&oscStatusLabel),
                             static_cast<juce::Component*>(&oscHostEditor),
                             static_cast<juce::Component*>(&oscPortEditor),
                             static_cast<juce::Component*>(&oscConnectButton),
                             static_cast<juce::Component*>(&oscDebugToggle),
                             static_cast<juce::Component*>(&eventMonitorLabel),
                             static_cast<juce::Component*>(&eventMonitor) })
    {
        component->setBounds({});
        component->setVisible(false);
    }

    transitionArea = transitionArea.reduced(12, 2);
    transitionCodeLabel.setBounds(transitionArea.removeFromTop(24));
    transitionCodeEditor.setBounds(transitionArea.reduced(2, 8));
}

void MainComponent::mouseMove(const juce::MouseEvent& event)
{
    updateSplitterCursor(event.getPosition());
}

void MainComponent::mouseExit(const juce::MouseEvent&)
{
    if (activeSplitterDrag == SplitterDrag::none)
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void MainComponent::mouseDown(const juce::MouseEvent& event)
{
    activeSplitterDrag = splitterAt(event.getPosition());

    if (activeSplitterDrag != SplitterDrag::none)
        updateSplitterCursor(event.getPosition());
}

void MainComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (activeSplitterDrag == SplitterDrag::none)
        return;

    const auto bounds = getLocalBounds().reduced(outerMargin);
    const auto contentTop = bounds.getY() + headerHeight + headerToTransitionGap;

    if (activeSplitterDrag == SplitterDrag::transition)
    {
        const auto availableHeight = bounds.getBottom() - contentTop;
        const auto maximumTransitionHeight = juce::jmax(minimumTransitionPaneHeight,
                                                        availableHeight - transitionSplitterThickness - minimumLowerPaneHeight);
        transitionPaneHeight = juce::jlimit(minimumTransitionPaneHeight,
                                            maximumTransitionHeight,
                                            event.getPosition().y - contentTop);
    }
    else if (activeSplitterDrag == SplitterDrag::grid)
    {
        const auto layout = calculateMainLayout();
        const auto lowerWidth = layout.statePane.getWidth() + layout.gridSplitter.getWidth() + layout.gridPane.getWidth();
        const auto usableWidth = juce::jmax(1, lowerWidth - gridSplitterThickness);
        const auto localX = event.getPosition().x - layout.statePane.getX();
        const auto stateWidth = juce::jlimit(minimumSplitPaneWidth,
                                             juce::jmax(minimumSplitPaneWidth, usableWidth - minimumSplitPaneWidth),
                                             localX);
        fsmGridSplitRatio = static_cast<double>(stateWidth) / static_cast<double>(usableWidth);
    }

    resized();
    repaint();
}

void MainComponent::mouseUp(const juce::MouseEvent& event)
{
    activeSplitterDrag = SplitterDrag::none;
    updateSplitterCursor(event.getPosition());
    repaint();
}

bool MainComponent::keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent)
{
    const auto keyCode = key.getKeyCode();
    const auto shortcut = key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown();
    const auto stateGraphFocused = originatingComponent == &stateGraph || stateGraph.hasKeyboardFocus(true);

    if (stateGraphFocused)
    {
        if (keyCode == juce::KeyPress::deleteKey || keyCode == juce::KeyPress::backspaceKey)
        {
            deleteSelectedState();
            return true;
        }

        const auto shortcutChar = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());

        if (shortcut && shortcutChar == 'c')
        {
            copySelectedState();
            return true;
        }

        if (shortcut && shortcutChar == 'v')
        {
            pasteCopiedState();
            return true;
        }
    }

    if (keyCode == juce::KeyPress::F5Key)
    {
        toggleTransportPlayback();
        return true;
    }

    if (keyCode == juce::KeyPress::F6Key)
    {
        stopTransport();
        return true;
    }

    if (keyCode == juce::KeyPress::F7Key)
    {
        resetTransport();
        return true;
    }

    if (keyCode == juce::KeyPress::F8Key)
    {
        showExampleMenu();
        return true;
    }

    if (keyCode == juce::KeyPress::F9Key)
    {
        showLoadPatternDialog();
        return true;
    }

    if (keyCode == juce::KeyPress::F10Key)
    {
        showSavePatternDialog();
        return true;
    }

    if (keyCode == juce::KeyPress::F11Key)
    {
        showRecentPatternMenu();
        return true;
    }

    if (keyCode == juce::KeyPress::F12Key)
    {
        triggerEmbeddedScTest();
        return true;
    }

    if (keyCode == juce::KeyPress::F1Key)
    {
        previousGridSlot();
        return true;
    }

    if (keyCode == juce::KeyPress::F2Key)
    {
        nextGridSlot();
        return true;
    }

    if (keyCode == juce::KeyPress::F3Key)
    {
        addGridSlot();
        return true;
    }

    if (keyCode == juce::KeyPress::F4Key)
    {
        addCompositionState();
        return true;
    }

    if (shortcut && keyCode >= static_cast<int>('1') && keyCode <= static_cast<int>('9'))
    {
        switchToGridSlot(keyCode - static_cast<int>('1'));
        return true;
    }

    return false;
}

void MainComponent::timerCallback()
{
    ++uiFrameCounter;
    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    if (lastTimerCallbackMs > 0.0)
        timerDeltaMs = nowMs - lastTimerCallbackMs;
    lastTimerCallbackMs = nowMs;

    if (eventMonitorDirty)
        refreshEventMonitor();

    if (pendingLaneCodeCompile && nowMs - lastLaneCodeEditMs > 700.0)
    {
        pendingLaneCodeCompile = false;
        compileSelectedScLane();
    }

    if (transportEngine.isPlaying())
    {
        repaint(0, 0, getWidth(), 92);
        repaint(0, juce::jmax(0, getHeight() - 76), getWidth(), 76);
    }

    bool metersChanged = false;
    for (int index = 0; index < maximumMixerChannels; ++index)
    {
        auto& peak = mixerMeterPeaks[static_cast<std::size_t>(index)];
        const auto raw = peak.exchange(0.0f, std::memory_order_acq_rel);
        auto& display = mixerMeterDisplay[static_cast<std::size_t>(index)];
        const auto target = juce::jlimit(0.0f, 1.0f, raw);
        const auto deltaSeconds = static_cast<float>(juce::jlimit(1.0, 60.0, timerDeltaMs) / 1000.0);
        const auto release = 1.0f - std::exp(-deltaSeconds / 0.105f);
        const auto next = target >= display ? target : display + (target - display) * release;
        const auto smoothed = target >= display ? next : juce::jmax(0.0f, next - 0.0012f);

        if (std::abs(smoothed - display) > 0.0005f || raw > 0.0005f)
            metersChanged = true;

        display = smoothed;
    }

    if (mixerViewVisible && metersChanged)
        refreshMixerMeters();

    if (exportCaptureComplete.exchange(false, std::memory_order_acq_rel))
        finishRealtimeWavExport();
}

void MainComponent::prepareToPlay(const int samplesPerBlockExpected, const double sampleRate)
{
    currentAudioSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    currentAudioBlockSize = samplesPerBlockExpected > 0 ? samplesPerBlockExpected : 512;

    if (embeddedScAudio.prepare(sampleRate, samplesPerBlockExpected, 2))
    {
        embeddedScAudio.setMasterLevel(masterLevel);
        statusLog.append("Embedded SuperCollider audio ready");

        juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<MainComponent>(this)]
        {
            if (safeThis != nullptr)
            {
                safeThis->compileEditableDefaultSynthDefs();
                safeThis->compileUserInstruments();
                safeThis->applyChannelMappingsToEngine();
                safeThis->compileScLanesForAllStates();
            }
        });
    }
    else
    {
        statusLog.append("Embedded SuperCollider unavailable: " + embeddedScAudio.getLastError());
    }

    repaint();
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    if (bufferToFill.buffer == nullptr)
        return;

    juce::AudioBuffer<float> output(bufferToFill.buffer->getArrayOfWritePointers(),
                                    bufferToFill.buffer->getNumChannels(),
                                    bufferToFill.startSample,
                                    bufferToFill.numSamples);
    audioCallbackCounter.fetch_add(1, std::memory_order_relaxed);
    audioSampleCounter.fetch_add(static_cast<std::uint64_t>(bufferToFill.numSamples), std::memory_order_relaxed);

    embeddedScAudio.render(output);

    float peak = 0.0f;
    for (int channel = 0; channel < output.getNumChannels(); ++channel)
        peak = juce::jmax(peak, output.getMagnitude(channel, 0, output.getNumSamples()));

    if (peak > 0.0f)
    {
        auto& meter = mixerMeterPeaks[static_cast<std::size_t>(maximumMixerChannels - 1)];
        const auto current = meter.load(std::memory_order_relaxed);
        if (peak > current)
            meter.store(juce::jlimit(0.0f, 1.0f, peak), std::memory_order_relaxed);
    }

    if (exportInProgress.load(std::memory_order_acquire))
    {
        const auto position = exportCaptureWritePosition.load(std::memory_order_relaxed);
        const auto remaining = exportCaptureTargetSamples - position;

        if (remaining > 0)
        {
            const auto samplesToCopy = static_cast<int>(juce::jmin<int64_t>(output.getNumSamples(), remaining));
            const std::lock_guard lock(exportCaptureMutex);

            if (exportCaptureBuffer != nullptr && position + samplesToCopy <= exportCaptureBuffer->getNumSamples())
            {
                for (int channel = 0; channel < 2; ++channel)
                {
                    const auto sourceChannel = juce::jmin(channel, output.getNumChannels() - 1);
                    exportCaptureBuffer->copyFrom(channel,
                                                  static_cast<int>(position),
                                                  output,
                                                  sourceChannel,
                                                  0,
                                                  samplesToCopy);
                }
            }

            const auto nextPosition = position + samplesToCopy;
            exportCaptureWritePosition.store(nextPosition, std::memory_order_release);

            if (nextPosition >= exportCaptureTargetSamples)
                exportCaptureComplete.store(true, std::memory_order_release);
        }
    }
}

void MainComponent::releaseResources()
{
    embeddedScAudio.release();
}

void MainComponent::configureOscControls()
{
    addAndMakeVisible(oscStatusLabel);
    addAndMakeVisible(oscHostEditor);
    addAndMakeVisible(oscPortEditor);
    addAndMakeVisible(oscConnectButton);
    addAndMakeVisible(oscDebugToggle);

    oscHostEditor.setText(oscOutput.getHost(), juce::dontSendNotification);
    oscPortEditor.setText(juce::String(oscOutput.getPort()), juce::dontSendNotification);
    oscConnectButton.setButtonText("CONNECT");
    oscDebugToggle.setButtonText("DEBUG");

    oscHostEditor.onReturnKey = [this] { connectOscFromControls(); };
    oscPortEditor.onReturnKey = [this] { connectOscFromControls(); };

    oscConnectButton.onClick = [this]
    {
        if (oscOutput.isConnected())
        {
            [[maybe_unused]] const auto disconnected = oscOutput.disconnect();
            statusLog.append("OSC disconnected");
        }
        else
        {
            connectOscFromControls();
        }

        updateOscStatusUi();
        repaint();
    };

    oscDebugToggle.onClick = [this]
    {
        oscOutput.setDebugMode(oscDebugToggle.getToggleState());
        statusLog.append(oscOutput.isDebugModeEnabled() ? "OSC debug on" : "OSC debug off");
    };

    styleOscControls();
    updateOscStatusUi();
}

void MainComponent::styleOscControls()
{
    const auto background = juce::Colour::fromRGB(46, 47, 48);
    const auto outline = lewittLine().withAlpha(0.58f);
    const auto text = lewittInk();

    for (auto* editor : { &oscHostEditor, &oscPortEditor })
    {
        editor->setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
        editor->setColour(juce::TextEditor::backgroundColourId, background);
        editor->setColour(juce::TextEditor::outlineColourId, outline);
        editor->setColour(juce::TextEditor::focusedOutlineColourId, text);
        editor->setColour(juce::TextEditor::textColourId, text);
        editor->setColour(juce::TextEditor::highlightColourId, lewittBlue().withAlpha(0.35f));
        editor->setJustification(juce::Justification::centred);
    }

    oscConnectButton.setColour(juce::TextButton::buttonColourId, background);
    oscConnectButton.setColour(juce::TextButton::buttonOnColourId, lewittBlue().withAlpha(0.35f));
    oscConnectButton.setColour(juce::TextButton::textColourOffId, text);
    oscConnectButton.setColour(juce::TextButton::textColourOnId, text);
    oscConnectButton.setColour(juce::ComboBox::outlineColourId, outline);

    oscDebugToggle.setColour(juce::ToggleButton::textColourId, text);
    oscDebugToggle.setColour(juce::ToggleButton::tickColourId, text);
    oscDebugToggle.setColour(juce::ToggleButton::tickDisabledColourId, outline);

    oscStatusLabel.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));
    oscStatusLabel.setJustificationType(juce::Justification::centredLeft);
    oscStatusLabel.setColour(juce::Label::textColourId, text);
}

void MainComponent::connectOscFromControls()
{
    const auto host = oscHostEditor.getText().trim();
    const auto port = oscPortEditor.getText().getIntValue();

    oscOutput.setEndpoint(host.isEmpty() ? "127.0.0.1" : host, juce::jlimit(1, 65535, port));

    if (oscOutput.connect())
        statusLog.append("OSC connected to " + oscOutput.getEndpointDescription());
    else
        statusLog.append("OSC connect failed: " + oscOutput.getEndpointDescription());

    updateOscStatusUi();
    repaint();
}

void MainComponent::updateOscStatusUi()
{
    oscConnectButton.setButtonText(oscOutput.isConnected() ? "DISCONNECT" : "CONNECT");
    oscStatusLabel.setText("OSC " + oscOutput.getConnectionStatusText(), juce::dontSendNotification);
    oscStatusLabel.setColour(juce::Label::textColourId, lewittInk());
}

void MainComponent::configureExampleControls()
{
    addAndMakeVisible(loadExampleButton);
    loadExampleButton.setButtonText("EXAMPLE");
    stylePatternButton(loadExampleButton);
    loadExampleButton.onClick = [this] { showExampleMenu(); };
}

void MainComponent::loadExampleFile(const juce::File& file)
{
    if (file.hasFileExtension(".gridcollider") || file.hasFileExtension(".json"))
    {
        loadCompositionFile(file);
        return;
    }

    const auto wasPlaying = transportEngine.isPlaying();

    if (wasPlaying)
        transportEngine.pause();

    storeActiveGridSlot();
    storeActiveTransitionCode();
    transportEngine.reset();
    lastTransportFrame = 0;
    lastTickInBeat = 0;
    activeStateEntryFrame = 0;
    gridEditor.clearPlayhead();

    juce::Result result = juce::Result::ok();

    {
        const std::lock_guard lock(gridRuntimeMutex);
        result = presetManager.load(file, gridModel);
    }

    if (result.wasOk())
    {
        storeActiveGridSlot();
        updateGridSlotControls();
        gridEditor.fitToView();
        statusLog.append("Loaded example: " + file.getFileNameWithoutExtension());
        addRecentPatternFile(file);
    }
    else
    {
        statusLog.append("Example load failed: " + result.getErrorMessage());
    }

    if (wasPlaying)
        transportEngine.start();

    repaint();
}

void MainComponent::loadFirstExample()
{
    const auto examples = presetManager.findExampleFiles();

    if (examples.isEmpty())
    {
        statusLog.append("No example files found");
        repaint();
        return;
    }

    loadExampleFile(examples.getFirst());
}

void MainComponent::showExampleMenu()
{
    const auto examples = presetManager.findExampleFiles();

    if (examples.isEmpty())
    {
        statusLog.append("No example files found");
        repaint();
        return;
    }

    juce::PopupMenu menu;

    for (int index = 0; index < examples.size(); ++index)
        menu.addItem(index + 1, exampleMenuTitleFor(examples[index]));

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(loadExampleButton),
                       [this, examples](const int result)
                       {
                           if (result <= 0 || result > examples.size())
                               return;

                           loadExampleFile(examples[result - 1]);
                       });
}

void MainComponent::configurePatternControls()
{
    addAndMakeVisible(loadPatternButton);
    addAndMakeVisible(savePatternButton);
    addAndMakeVisible(recentPatternButton);
    addAndMakeVisible(embeddedScTestButton);

    loadPatternButton.setButtonText("LOAD");
    savePatternButton.setButtonText("SAVE");
    recentPatternButton.setButtonText("RECENT");
    embeddedScTestButton.setButtonText("TEST");

    for (auto* button : { &loadPatternButton, &savePatternButton, &recentPatternButton, &embeddedScTestButton })
        stylePatternButton(*button);

    loadPatternButton.onClick = [this] { showLoadPatternDialog(); };
    savePatternButton.onClick = [this] { showSavePatternDialog(); };
    recentPatternButton.onClick = [this] { showRecentPatternMenu(); };
    embeddedScTestButton.onClick = [this] { triggerEmbeddedScTest(); };
}

void MainComponent::stylePatternButton(juce::TextButton& button)
{
    button.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(48, 49, 50));
    button.setColour(juce::TextButton::buttonOnColourId, lewittBlue().withAlpha(0.86f));
    button.setColour(juce::TextButton::textColourOffId, lewittInk());
    button.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    button.setColour(juce::ComboBox::outlineColourId, lewittLine().withAlpha(0.62f));
}

void MainComponent::showLoadCompositionDialog()
{
    const auto start = currentCompositionFile != juce::File()
                           ? currentCompositionFile.getParentDirectory()
                           : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    fileChooser = std::make_unique<juce::FileChooser>("Load GridCollider composition", start, "*.gridcollider;*.json;*.orca");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [safeThis = juce::Component::SafePointer<MainComponent>(this)](const juce::FileChooser& chooser)
                             {
                                 if (safeThis == nullptr)
                                     return;

                                 const auto file = chooser.getResult();

                                 if (file.existsAsFile())
                                     safeThis->loadCompositionFile(file);
                             });
}

void MainComponent::showSaveCompositionDialog()
{
    auto start = currentCompositionFile != juce::File()
                     ? currentCompositionFile
                     : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("GridCollider composition.gridcollider");
    fileChooser = std::make_unique<juce::FileChooser>("Save GridCollider composition", start, "*.gridcollider");
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                 | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                             [safeThis = juce::Component::SafePointer<MainComponent>(this)](const juce::FileChooser& chooser)
                             {
                                 if (safeThis == nullptr)
                                     return;

                                 auto file = chooser.getResult();

                                 if (file != juce::File())
                                     safeThis->saveCompositionFile(file);
                             });
}

void MainComponent::showExportWavDialog(const double durationSeconds)
{
    const auto defaultName = currentCompositionFile != juce::File()
                                 ? currentCompositionFile.getFileNameWithoutExtension() + ".wav"
                                 : "GridCollider export.wav";
    const auto start = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(defaultName);

    fileChooser = std::make_unique<juce::FileChooser>("Export stereo WAV", start, "*.wav");
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                 | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                             [safeThis = juce::Component::SafePointer<MainComponent>(this), durationSeconds](const juce::FileChooser& chooser)
                             {
                                 if (safeThis == nullptr)
                                     return;

                                 auto file = chooser.getResult();

                                 if (file != juce::File())
                                     safeThis->exportStereoWav(file, durationSeconds);
                             });
}

void MainComponent::exportStereoWav(juce::File file, const double durationSeconds)
{
    if (file.getFileExtension().isEmpty())
        file = file.withFileExtension("wav");

    if (exportInProgress.exchange(true, std::memory_order_acq_rel))
    {
        statusLog.append("WAV export already running");
        repaint();
        return;
    }

    storeActiveGridSlot();
    storeActiveTransitionCode();
    storeActiveLane();

    const auto sampleRate = currentAudioSampleRate > 0.0 ? currentAudioSampleRate : 44100.0;
    const auto targetSamples = static_cast<int64_t>(std::llround(juce::jlimit(1.0, 3600.0, durationSeconds) * sampleRate));

    if (targetSamples <= 0 || targetSamples > static_cast<int64_t>(std::numeric_limits<int>::max()))
    {
        exportCaptureActive.store(false, std::memory_order_release);
        exportInProgress.store(false, std::memory_order_release);
        statusLog.append("WAV export failed: duration is too long");
        repaint();
        return;
    }

    auto capture = std::make_unique<juce::AudioBuffer<float>>(2, static_cast<int>(targetSamples));
    capture->clear();

    {
        const std::lock_guard lock(exportCaptureMutex);
        exportCaptureFile = file;
        exportCaptureSampleRate = sampleRate;
        exportCaptureTargetSamples = targetSamples;
        exportCaptureWritePosition.store(0, std::memory_order_release);
        exportCaptureComplete.store(false, std::memory_order_release);
        exportCaptureActive.store(false, std::memory_order_release);
        exportCaptureBuffer = std::move(capture);
    }

    exportStartedTransport = ! transportEngine.isPlaying();

    if (exportStartedTransport)
    {
        resetTransport();
        compileScLanesForAllStates();
        transportEngine.start();
        updateTransportControls();
    }

    exportCaptureActive.store(true, std::memory_order_release);
    statusLog.append("Recording master WAV: " + juce::String(durationSeconds, 1) + " seconds");
    repaint();
}

void MainComponent::finishRealtimeWavExport()
{
    juce::File file;
    double sampleRate = 44100.0;
    int samplesToWrite = 0;
    std::unique_ptr<juce::AudioBuffer<float>> capture;
    const auto shouldStopTransport = exportStartedTransport;

    {
        const std::lock_guard lock(exportCaptureMutex);
        exportCaptureActive.store(false, std::memory_order_release);
        file = exportCaptureFile;
        sampleRate = exportCaptureSampleRate;
        samplesToWrite = static_cast<int>(juce::jmin<int64_t>(exportCaptureTargetSamples,
                                                              exportCaptureWritePosition.load(std::memory_order_acquire)));
        capture = std::move(exportCaptureBuffer);
        exportCaptureTargetSamples = 0;
        exportCaptureFile = juce::File();
        exportStartedTransport = false;
    }

    if (shouldStopTransport)
    {
        transportEngine.stop();
        updateTransportControls();
    }

    if (capture == nullptr || samplesToWrite <= 0)
    {
        exportCaptureActive.store(false, std::memory_order_release);
        exportInProgress.store(false, std::memory_order_release);
        statusLog.append("WAV export failed: no captured audio");
        repaint();
        return;
    }

    std::thread([safeThis = juce::Component::SafePointer<MainComponent>(this),
                 file,
                 sampleRate,
                 samplesToWrite,
                 capturedAudio = std::move(capture)]() mutable
    {
        juce::String message;
        auto peak = 0.0f;
        for (int channel = 0; channel < capturedAudio->getNumChannels(); ++channel)
            peak = juce::jmax(peak, capturedAudio->getMagnitude(channel, 0, samplesToWrite));

        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::OutputStream> outputStream = file.createOutputStream();

        if (outputStream == nullptr)
        {
            message = "WAV export failed: could not write " + file.getFileName();
        }
        else
        {
            auto writer = wavFormat.createWriterFor(outputStream,
                                                    juce::AudioFormatWriterOptions()
                                                        .withSampleRate(sampleRate)
                                                        .withChannelLayout(juce::AudioChannelSet::stereo())
                                                        .withBitsPerSample(24));

            if (writer == nullptr)
            {
                message = "WAV export failed: could not create WAV writer";
            }
            else
            {
                writer->writeFromAudioSampleBuffer(*capturedAudio, 0, samplesToWrite);
                writer.reset();
                const auto detail = " peak " + juce::String(peak, 5) + " samples " + juce::String(samplesToWrite);
                message = peak > 0.0001f
                              ? "Exported WAV: " + file.getFileName() + detail
                              : "Exported WAV appears silent: " + file.getFileName() + detail;
            }
        }

        juce::MessageManager::callAsync([safeThis, message]
        {
            if (safeThis == nullptr)
                return;

            safeThis->exportInProgress.store(false, std::memory_order_release);
            safeThis->statusLog.append(message);
            safeThis->refreshMixerView();
            safeThis->repaint();
        });
    }).detach();
}

juce::String MainComponent::snapshotToText(const GridModel::Snapshot& snapshot) const
{
    juce::String text;

    for (int row = 0; row < snapshot.height; ++row)
    {
        for (int column = 0; column < snapshot.width; ++column)
            text += juce::String::charToString(snapshot.getGlyph(column, row));

        if (row + 1 < snapshot.height)
            text += "\n";
    }

    return text;
}

GridModel::Snapshot MainComponent::snapshotFromText(const juce::String& text, const int width, const int height) const
{
    auto snapshot = makeEmptyGridSnapshot(width, height);
    const auto lines = juce::StringArray::fromLines(text);

    for (int row = 0; row < juce::jmin(snapshot.height, lines.size()); ++row)
    {
        const auto line = lines[row];

        for (int column = 0; column < juce::jmin(snapshot.width, line.length()); ++column)
        {
            const auto character = line[column];
            snapshot.cells[static_cast<std::size_t>(column + row * snapshot.width)] =
                character >= 32 && character <= 126 ? static_cast<char>(character) : GridModel::emptyGlyph;
        }
    }

    return snapshot;
}

juce::var MainComponent::serialiseComposition() const
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("format", "GridColliderComposition");
    root->setProperty("version", 1);
    root->setProperty("activeState", activeStateIndex);
    root->setProperty("activeLane", activeGridSlot);
    root->setProperty("masterLevel", masterLevel);

    juce::Array<juce::var> channelMapArray;
    for (const auto& instrument : channelInstrumentMap)
        channelMapArray.add(instrument);
    root->setProperty("channelInstrumentMap", channelMapArray);

    juce::Array<juce::var> userInstrumentArray;
    for (const auto& instrument : userInstruments)
    {
        auto instrumentObject = std::make_unique<juce::DynamicObject>();
        instrumentObject->setProperty("name", instrument.name);
        instrumentObject->setProperty("code", instrument.code);
        userInstrumentArray.add(juce::var(instrumentObject.release()));
    }
    root->setProperty("userInstruments", userInstrumentArray);

    juce::Array<juce::var> defaultInstrumentArray;
    for (const auto& instrument : defaultInstruments)
    {
        auto instrumentObject = std::make_unique<juce::DynamicObject>();
        instrumentObject->setProperty("name", instrument.name);
        instrumentObject->setProperty("code", instrument.code);
        defaultInstrumentArray.add(juce::var(instrumentObject.release()));
    }
    root->setProperty("defaultSynthDefs", defaultInstrumentArray);

    juce::Array<juce::var> statesArray;

    for (const auto& state : compositionStates)
    {
        auto stateObject = std::make_unique<juce::DynamicObject>();
        stateObject->setProperty("name", state.name);
        stateObject->setProperty("bpm", state.bpm);
        stateObject->setProperty("advanceMode", getStateAdvanceModeText(state.advanceMode));
        stateObject->setProperty("advanceInterval", state.advanceInterval);
        stateObject->setProperty("timeSignatureNumerator", state.timeSignatureNumerator);
        stateObject->setProperty("timeSignatureDenominator", state.timeSignatureDenominator);
        stateObject->setProperty("transitionCode", state.transitionCode);

        juce::Array<juce::var> lanesArray;

        for (const auto& lane : state.grids)
        {
            auto laneObject = std::make_unique<juce::DynamicObject>();
            laneObject->setProperty("kind", lane.kind == CompositionGrid::Kind::grid ? "grid" : "supercollider");
            laneObject->setProperty("tempoRatio", lane.tempoRatio);
            laneObject->setProperty("phaseOffsetEnabled", lane.phaseOffsetEnabled);
            laneObject->setProperty("phaseOffsetDegrees", lane.phaseOffsetDegrees);
            laneObject->setProperty("mixerLevel", lane.mixerLevel);
            laneObject->setProperty("mixerPan", lane.mixerPan);
            laneObject->setProperty("scCode", lane.scCode);
            laneObject->setProperty("scSynthName", lane.scSynthName);
            laneObject->setProperty("gridWidth", lane.snapshot.width);
            laneObject->setProperty("gridHeight", lane.snapshot.height);
            laneObject->setProperty("gridText", snapshotToText(lane.snapshot));
            lanesArray.add(juce::var(laneObject.release()));
        }

        stateObject->setProperty("lanes", lanesArray);
        statesArray.add(juce::var(stateObject.release()));
    }

    root->setProperty("states", statesArray);
    return juce::var(root.release());
}

juce::Result MainComponent::restoreComposition(const juce::var& document)
{
    if (! document.isObject())
        return juce::Result::fail("Composition file is not a JSON object");

    const auto* root = document.getDynamicObject();
    if (root == nullptr || root->getProperty("format").toString() != "GridColliderComposition")
        return juce::Result::fail("Not a GridCollider composition file");

    const auto* statesVar = root->getProperty("states").getArray();
    if (statesVar == nullptr || statesVar->isEmpty())
        return juce::Result::fail("Composition has no states");

    initialiseDefaultInstrumentLayer();

    if (const auto* mapVar = root->getProperty("channelInstrumentMap").getArray())
    {
        for (int channel = 0; channel < juce::jmin(instrumentChannelCount, mapVar->size()); ++channel)
        {
            auto instrument = mapVar->getReference(channel).toString().trim();
            if (instrument.isNotEmpty())
                channelInstrumentMap[static_cast<std::size_t>(channel)] = instrument;
        }
    }

    if (const auto* instrumentVar = root->getProperty("userInstruments").getArray())
    {
        std::vector<UserInstrument> loadedInstruments;
        loadedInstruments.reserve(static_cast<std::size_t>(instrumentVar->size()));

        for (const auto& entry : *instrumentVar)
        {
            const auto* object = entry.getDynamicObject();
            if (object == nullptr)
                continue;

            auto name = object->getProperty("name").toString().retainCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_").trim();
            auto code = object->getProperty("code").toString();

            if (name.isEmpty())
                name = "gc_user_" + juce::String(static_cast<int>(loadedInstruments.size()) + 1).paddedLeft('0', 2);

            if (code.isEmpty())
                code = createDefaultUserInstrumentCode(name);

            loadedInstruments.push_back({ name, code });
        }

        if (! loadedInstruments.empty())
            userInstruments = std::move(loadedInstruments);
    }

    if (const auto* defaultVar = root->getProperty("defaultSynthDefs").getArray())
    {
        for (const auto& entry : *defaultVar)
        {
            const auto* object = entry.getDynamicObject();
            if (object == nullptr)
                continue;

            const auto name = object->getProperty("name").toString().retainCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_").trim();
            const auto code = object->getProperty("code").toString();

            if (name.isEmpty() || code.isEmpty())
                continue;

            for (auto& instrument : defaultInstruments)
            {
                if (instrument.name.equalsIgnoreCase(name))
                {
                    instrument.code = code;
                    break;
                }
            }
        }
    }

    selectedUserInstrumentIndex = userInstruments.empty() ? -1 : 0;
    selectedDefaultInstrumentIndex = defaultInstruments.empty() ? -1 : 0;
    selectedInstrumentLaneReferenceIndex = -1;

    std::vector<CompositionState> loadedStates;
    loadedStates.reserve(static_cast<std::size_t>(juce::jmin(maximumCompositionStates, statesVar->size())));

    for (int stateIndex = 0; stateIndex < juce::jmin(maximumCompositionStates, statesVar->size()); ++stateIndex)
    {
        const auto stateVar = statesVar->getReference(stateIndex);
        const auto* stateObject = stateVar.getDynamicObject();

        if (stateObject == nullptr)
            continue;

        CompositionState state;
        state.name = stateObject->getProperty("name").toString();
        if (state.name.isEmpty())
            state.name = "State " + juce::String(stateIndex + 1).paddedLeft('0', 2);

        state.bpm = juce::jlimit(20.0, 320.0, static_cast<double>(stateObject->getProperty("bpm")));
        state.advanceInterval = juce::jlimit(1, 256, static_cast<int>(stateObject->getProperty("advanceInterval")));
        const auto numeratorVar = stateObject->getProperty("timeSignatureNumerator");
        const auto denominatorVar = stateObject->getProperty("timeSignatureDenominator");
        state.timeSignatureNumerator = juce::jlimit(1, 32, numeratorVar.isVoid() ? 4 : static_cast<int>(numeratorVar));
        state.timeSignatureDenominator = juce::jlimit(1, 32, denominatorVar.isVoid() ? 4 : static_cast<int>(denominatorVar));
        state.transitionCode = stateObject->getProperty("transitionCode").toString();
        if (state.transitionCode.isEmpty())
            state.transitionCode = createDefaultTransitionCode(stateIndex + 1);

        const auto mode = stateObject->getProperty("advanceMode").toString().toLowerCase();
        if (mode == "beats")
            state.advanceMode = CompositionState::AdvanceMode::beats;
        else if (mode == "bars")
            state.advanceMode = CompositionState::AdvanceMode::bars;
        else if (mode == "trigger")
            state.advanceMode = CompositionState::AdvanceMode::trigger;
        else
            state.advanceMode = CompositionState::AdvanceMode::manual;

        if (const auto* lanesVar = stateObject->getProperty("lanes").getArray())
        {
            for (int laneIndex = 0; laneIndex < juce::jmin(maximumGridsPerState, lanesVar->size()); ++laneIndex)
            {
                const auto laneVar = lanesVar->getReference(laneIndex);
                const auto* laneObject = laneVar.getDynamicObject();

                if (laneObject == nullptr)
                    continue;

                CompositionGrid lane;
                lane.kind = laneObject->getProperty("kind").toString() == "supercollider"
                                ? CompositionGrid::Kind::supercollider
                                : CompositionGrid::Kind::grid;
                lane.tempoRatio = juce::jlimit(minimumGridTempoRatio, maximumGridTempoRatio, static_cast<double>(laneObject->getProperty("tempoRatio")));
                lane.phaseOffsetEnabled = static_cast<bool>(laneObject->getProperty("phaseOffsetEnabled"));
                lane.phaseOffsetDegrees = juce::jlimit(0.0, 360.0, static_cast<double>(laneObject->getProperty("phaseOffsetDegrees")));
                lane.mixerLevel = juce::jlimit(0.0f, 1.25f, static_cast<float>(static_cast<double>(laneObject->getProperty("mixerLevel"))));
                lane.mixerPan = juce::jlimit(-1.0f, 1.0f, static_cast<float>(static_cast<double>(laneObject->getProperty("mixerPan"))));
                lane.scCode = laneObject->getProperty("scCode").toString();
                lane.scSynthName = laneObject->getProperty("scSynthName").toString();
                lane.scCodeDirty = lane.kind == CompositionGrid::Kind::supercollider;
                lane.snapshot = snapshotFromText(laneObject->getProperty("gridText").toString(),
                                                 static_cast<int>(laneObject->getProperty("gridWidth")),
                                                 static_cast<int>(laneObject->getProperty("gridHeight")));
                state.grids.push_back(std::move(lane));
            }
        }

        if (state.grids.empty())
            state.grids.push_back({ makeEmptyGridSnapshot() });

        loadedStates.push_back(std::move(state));
    }

    if (loadedStates.empty())
        return juce::Result::fail("Composition did not contain readable states");

    compositionStates = std::move(loadedStates);
    activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, static_cast<int>(root->getProperty("activeState")));
    activeGridSlot = juce::jlimit(0,
                                  static_cast<int>(compositionStates[static_cast<std::size_t>(activeStateIndex)].grids.size()) - 1,
                                  static_cast<int>(root->getProperty("activeLane")));
    masterLevel = juce::jlimit(0.0f, 1.25f, static_cast<float>(static_cast<double>(root->getProperty("masterLevel"))));
    return juce::Result::ok();
}

void MainComponent::loadCompositionFile(const juce::File& file)
{
    if (file.hasFileExtension(".orca"))
    {
        loadPatternFile(file, true);
        return;
    }

    const auto wasPlaying = transportEngine.isPlaying();
    if (wasPlaying)
        transportEngine.pause();

    const auto text = file.loadFileAsString();
    juce::var parsed = juce::JSON::parse(text);

    if (parsed.isVoid())
    {
        statusLog.append("Composition load failed: invalid JSON");
        repaint();
        return;
    }

    juce::Result result = juce::Result::ok();
    {
        const std::lock_guard lock(gridRuntimeMutex);
        result = restoreComposition(parsed);

        if (result.wasOk())
        {
            auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];
            gridModel.applySnapshot(state.grids[static_cast<std::size_t>(activeGridSlot)].snapshot);
        }
    }

    if (result.wasOk())
    {
        currentCompositionFile = file;
        transportEngine.reset();
        lastTransportFrame = 0;
        lastTickInBeat = 0;
        activeStateEntryFrame = 0;
        resetGridRuntimeClocks();
        embeddedScAudio.setMasterLevel(masterLevel);
        updateTransportControls();
        updateGridSlotControls();
        showActiveTransitionCode();
        compileEditableDefaultSynthDefs();
        compileUserInstruments();
        compileScLanesForAllStates();
        applyChannelMappingsToEngine();
        refreshInstrumentView();
        refreshMixerView();
        gridEditor.fitToView();
        statusLog.append("Loaded composition: " + file.getFileName());
        addRecentPatternFile(file);
    }
    else
    {
        statusLog.append("Composition load failed: " + result.getErrorMessage());
    }

    if (wasPlaying)
        transportEngine.start();

    repaint();
}

void MainComponent::saveCompositionFile(juce::File file)
{
    if (file.getFileExtension().isEmpty())
        file = file.withFileExtension("gridcollider");

    storeActiveGridSlot();
    storeActiveTransitionCode();
    storeActiveInstrumentEditor();
    if (instrumentsViewVisible)
    {
        for (int channel = 0; channel < instrumentChannelCount; ++channel)
        {
            auto name = instrumentChannelSelectors[static_cast<std::size_t>(channel)].getText().trim();
            if (name.isNotEmpty())
                channelInstrumentMap[static_cast<std::size_t>(channel)] = name;
        }
        applyChannelMappingsToEngine();
    }

    juce::var document;
    {
        const std::lock_guard lock(gridRuntimeMutex);
        document = serialiseComposition();
    }

    const auto json = juce::JSON::toString(document, true);

    if (file.replaceWithText(json))
    {
        currentCompositionFile = file;
        statusLog.append("Saved composition: " + file.getFileName());
        addRecentPatternFile(file);
    }
    else
    {
        statusLog.append("Composition save failed: " + file.getFullPathName());
    }

    repaint();
}

void MainComponent::showLoadPatternDialog()
{
    const auto start = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    fileChooser = std::make_unique<juce::FileChooser>("Load GridCollider pattern", start, "*.orca");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [safeThis = juce::Component::SafePointer<MainComponent>(this)](const juce::FileChooser& chooser)
                             {
                                 if (safeThis == nullptr)
                                     return;

                                 const auto file = chooser.getResult();

                                 if (file.existsAsFile())
                                     safeThis->loadPatternFile(file, true);
                             });
}

void MainComponent::showSavePatternDialog()
{
    auto start = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("gridcollider-pattern.orca");
    fileChooser = std::make_unique<juce::FileChooser>("Save GridCollider pattern", start, "*.orca");
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                 | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                             [safeThis = juce::Component::SafePointer<MainComponent>(this)](const juce::FileChooser& chooser)
                             {
                                 if (safeThis == nullptr)
                                     return;

                                 auto file = chooser.getResult();

                                 if (file != juce::File())
                                     safeThis->savePatternFile(file);
                             });
}

void MainComponent::loadPatternFile(const juce::File& file, const bool addToRecent)
{
    const auto wasPlaying = transportEngine.isPlaying();

    if (wasPlaying)
        transportEngine.pause();

    storeActiveGridSlot();
    transportEngine.reset();
    lastTransportFrame = 0;
    lastTickInBeat = 0;
    gridEditor.clearPlayhead();

    juce::Result result = juce::Result::ok();

    {
        const std::lock_guard lock(gridRuntimeMutex);
        result = presetManager.load(file, gridModel);
    }

    if (result.wasOk())
    {
        storeActiveGridSlot();
        updateGridSlotControls();
        gridEditor.fitToView();
        statusLog.append("Loaded pattern: " + file.getFileName());

        if (addToRecent)
            addRecentPatternFile(file);
    }
    else
    {
        statusLog.append("Pattern load failed: " + result.getErrorMessage());
    }

    if (wasPlaying)
        transportEngine.start();

    repaint();
}

void MainComponent::savePatternFile(juce::File file)
{
    if (file.getFileExtension().isEmpty())
        file = file.withFileExtension("orca");

    storeActiveGridSlot();
    juce::Result result = juce::Result::ok();

    {
        const std::lock_guard lock(gridRuntimeMutex);
        result = presetManager.save(file, gridModel);
    }

    if (result.wasOk())
    {
        statusLog.append("Saved pattern: " + file.getFileName());
        addRecentPatternFile(file);
    }
    else
    {
        statusLog.append("Pattern save failed: " + result.getErrorMessage());
    }

    repaint();
}

void MainComponent::addRecentPatternFile(const juce::File& file)
{
    for (int index = recentPatternFiles.size(); --index >= 0;)
    {
        if (recentPatternFiles[index] == file)
            recentPatternFiles.remove(index);
    }

    recentPatternFiles.insert(0, file);

    while (recentPatternFiles.size() > 8)
        recentPatternFiles.removeLast();
}

void MainComponent::showRecentPatternMenu()
{
    juce::PopupMenu menu;

    if (recentPatternFiles.isEmpty())
    {
        menu.addItem(1, "No recent patterns", false);
    }
    else
    {
        for (int index = 0; index < recentPatternFiles.size(); ++index)
            menu.addItem(index + 1, recentPatternFiles[index].getFileName());
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(recentPatternButton),
                       [this](const int result)
                       {
                           if (result <= 0 || result > recentPatternFiles.size())
                               return;

                           loadPatternFile(recentPatternFiles[result - 1], true);
                       });
}

void MainComponent::triggerEmbeddedScTest()
{
    if (! embeddedScAudio.isReady())
    {
        statusLog.append("Embedded SC not ready: " + embeddedScAudio.getLastError());
        repaint();
        return;
    }

    EventFields fields;
    fields.timestampSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    fields.tick = lastTransportFrame;
    fields.sourceCell = { 32, 16 };
    fields.instrumentName = "tone";
    fields.pitch = 60;
    fields.velocity = 0.9f;
    fields.durationTicks = 1;

    embeddedScAudio.setTransport(transportEngine.getBpm(), lastTransportFrame, transportEngine.isPlaying());
    embeddedScAudio.enqueue({ InternalEvent { NoteEvent { fields } } });
    statusLog.append("Embedded SC test note");
    repaint();
}

void MainComponent::configureTransitionCodePane()
{
    addAndMakeVisible(transitionCodeLabel);
    addAndMakeVisible(transitionCodeEditor);
    transitionCodeBackdrop.setVisible(false);

    transitionCodeLabel.setText("TRANSITIONS.SC", juce::dontSendNotification);
    transitionCodeLabel.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));
    transitionCodeLabel.setJustificationType(juce::Justification::centredLeft);

    transitionCodeEditor.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    transitionCodeEditor.setTabSize(4, true);
    transitionCodeEditor.setScrollbarThickness(8);
    transitionCodeDocumentListener.setCallback([this]
    {
        if (updatingTransitionCodeEditor)
            return;

        storeActiveTransitionCode();
        refreshStateGraph();
        refreshArrangementView();
    });
    transitionCodeDocument.addListener(&transitionCodeDocumentListener);

    styleTransitionCodePane();
    showActiveTransitionCode();
}

void MainComponent::styleTransitionCodePane()
{
    transitionCodeLabel.setColour(juce::Label::textColourId, lewittInk());
    transitionCodeEditor.setColour(juce::CodeEditorComponent::backgroundColourId, juce::Colour::fromRGB(24, 25, 26));
    transitionCodeEditor.setColour(juce::CodeEditorComponent::defaultTextColourId, lewittInk());
    transitionCodeEditor.setColour(juce::CodeEditorComponent::highlightColourId, lewittBlue().withAlpha(0.34f));
    transitionCodeEditor.setColour(juce::CodeEditorComponent::lineNumberBackgroundId, juce::Colour::fromRGB(34, 35, 36));
    transitionCodeEditor.setColour(juce::CodeEditorComponent::lineNumberTextId, lewittInk().withAlpha(0.42f));
    transitionCodeEditor.setColour(juce::ScrollBar::backgroundColourId, juce::Colour::fromRGB(35, 36, 37));
    transitionCodeEditor.setColour(juce::ScrollBar::thumbColourId, lewittBlue());
    transitionCodeEditor.setColourScheme(scCodeTokeniser.getDefaultColourScheme());
}

void MainComponent::storeActiveTransitionCode()
{
    const std::lock_guard lock(gridRuntimeMutex);

    if (compositionStates.empty())
        return;

    activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
    compositionStates[static_cast<std::size_t>(activeStateIndex)].transitionCode = transitionCodeDocument.getAllContent();
}

void MainComponent::showActiveTransitionCode()
{
    juce::String code;

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (compositionStates.empty())
            code = createDefaultTransitionCode(1);
        else
        {
            const auto index = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
            auto& state = compositionStates[static_cast<std::size_t>(index)];

            if (state.transitionCode.isEmpty())
                state.transitionCode = createDefaultTransitionCode(index + 1);

            code = state.transitionCode;
        }
    }

    updatingTransitionCodeEditor = true;
    transitionCodeDocument.replaceAllContent(code);
    updatingTransitionCodeEditor = false;
}

juce::String MainComponent::createDefaultTransitionCode(const int stateNumber) const
{
    const auto nextState = stateNumber >= maximumCompositionStates ? 1 : stateNumber + 1;

    return "// TRANSITIONS.SC - State "
        + juce::String(stateNumber)
        + "\n// This state owns this rule. States are 1-based.\n"
          "// Use linear edges, weighted edges, cycles, trigger exits, or leave maps empty.\n"
          "// Conditions may read: state, tick, beat, bar, frame, trigger.\n"
          "// Example condition: when: \"bar % 4 == 0\"\n\n"
          "~linear = (\n"
          "    "
        + juce::String(stateNumber)
        + ": "
        + juce::String(nextState)
        + "\n"
          ");\n\n"
          "~weighted = (\n"
          "    // "
        + juce::String(stateNumber)
        + ": [\n"
          "    //     (to: "
        + juce::String(nextState)
        + ", chance: 0.70, when: \"bar % 4 == 0\"),\n"
          "    //     (to: "
        + juce::String(stateNumber)
        + ", chance: 0.30)\n"
          "    // ]\n"
          ");\n\n"
          "~cycle = [\n"
          "    // 1, 2, 3, 4\n"
          "];\n\n"
          "~trigger = (\n"
          "    // fill: "
        + juce::String(nextState)
        + ",\n"
          "    // drop: [\n"
          "    //     (to: "
        + juce::String(nextState)
        + ", chance: 0.60),\n"
          "    //     (to: 1, chance: 0.40)\n"
          "    // ]\n"
          ");\n";
}

void MainComponent::configureEventMonitor()
{
    addAndMakeVisible(eventMonitorLabel);
    addAndMakeVisible(eventMonitor);

    eventMonitorLabel.setText("EVENTS", juce::dontSendNotification);
    eventMonitorLabel.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));
    eventMonitorLabel.setJustificationType(juce::Justification::centredLeft);
    eventMonitorLabel.setColour(juce::Label::textColourId, lewittInk());

    eventMonitor.setReadOnly(true);
    eventMonitor.setMultiLine(true);
    eventMonitor.setScrollbarsShown(false);
    eventMonitor.setCaretVisible(false);
    eventMonitor.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
    eventMonitor.setColour(juce::TextEditor::backgroundColourId, juce::Colour::fromRGB(24, 25, 26));
    eventMonitor.setColour(juce::TextEditor::outlineColourId, lewittLine().withAlpha(0.58f));
    eventMonitor.setColour(juce::TextEditor::focusedOutlineColourId, lewittBlue());
    eventMonitor.setColour(juce::TextEditor::textColourId, lewittInk());
    eventMonitor.setColour(juce::TextEditor::highlightColourId, lewittBlue().withAlpha(0.35f));
}

void MainComponent::appendEventMonitorLine(const LogEvent& event)
{
    eventMonitorLines.add("[" + juce::String(event.fields.tick).paddedLeft('0', 4) + "] " + event.message);

    while (eventMonitorLines.size() > 80)
        eventMonitorLines.remove(0);

    eventMonitorDirty = true;
}

void MainComponent::refreshEventMonitor()
{
    eventMonitorDirty = false;
    eventMonitor.setText(eventMonitorLines.joinIntoString("\n"), juce::dontSendNotification);
    eventMonitor.moveCaretToEnd(false);
}

void MainComponent::configureTransport()
{
    transportEngine.setBpm(120.0);
    transportEngine.setTicksPerBeat(4);

    transportEngine.setEvaluationCallback([this](const TransportEngine::TickContext& context)
    {
        return evaluateActiveState(context);
    });

    transportEngine.setTickCallback([safeThis = juce::Component::SafePointer<MainComponent>(this)](const TransportEngine::TickResult& result)
    {
        if (safeThis != nullptr)
            safeThis->advanceStateFromTransitionPane(result);

        juce::MessageManager::callAsync([safeThis, result]
        {
            if (safeThis != nullptr)
                safeThis->handleTransportTick(result);
        });
    });
}

void MainComponent::configureTransportControls()
{
    addAndMakeVisible(playPauseButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(resetButton);
    addAndMakeVisible(bpmEditor);
    addAndMakeVisible(bpmLabel);

    playPauseButton.onClick = [this] { toggleTransportPlayback(); };
    stopButton.onClick = [this] { stopTransport(); };
    resetButton.onClick = [this] { resetTransport(); };

    bpmLabel.setText("BPM", juce::dontSendNotification);
    bpmEditor.setText(juce::String(transportEngine.getBpm(), 1), juce::dontSendNotification);
    forceBlackEditorText(bpmEditor);
    bpmEditor.onReturnKey = [this] { applyTransportEditors(); };
    bpmEditor.onFocusLost = [this] { applyTransportEditors(); };

    styleTransportControls();
    updateTransportControls();
}

void MainComponent::configureStateGraph()
{
    stateGraph.onStateSelected = [this](const int index) { switchToState(index); };
    stateGraph.onAddStateRequested = [this] { addCompositionState(); };

    addAndMakeVisible(stateSlotLabel);
    addAndMakeVisible(previousStateButton);
    addAndMakeVisible(nextStateButton);
    addAndMakeVisible(addStateButton);
    addAndMakeVisible(stateAdvanceLabel);
    addAndMakeVisible(stateAdvanceModeButton);
    addAndMakeVisible(stateAdvanceIntervalEditor);
    addAndMakeVisible(stateTimeSignatureLabel);
    addAndMakeVisible(stateTimeSignatureNumeratorEditor);
    addAndMakeVisible(stateTimeSignatureSeparatorLabel);
    addAndMakeVisible(stateTimeSignatureDenominatorEditor);

    previousStateButton.setButtonText("<");
    nextStateButton.setButtonText(">");
    addStateButton.setButtonText("+");
    stateAdvanceLabel.setText("ADV", juce::dontSendNotification);
    stateTimeSignatureLabel.setText("SIG", juce::dontSendNotification);
    stateTimeSignatureSeparatorLabel.setText("/", juce::dontSendNotification);

    previousStateButton.setTooltip("Previous state");
    nextStateButton.setTooltip("Next state");
    addStateButton.setTooltip("Add state");
    stateAdvanceModeButton.setTooltip("Selected state advance mode");
    stateAdvanceIntervalEditor.setTooltip("Selected state advance interval");
    stateTimeSignatureNumeratorEditor.setTooltip("Selected state time signature numerator");
    stateTimeSignatureDenominatorEditor.setTooltip("Selected state time signature denominator");

    previousStateButton.onClick = [this] { previousState(); };
    nextStateButton.onClick = [this] { nextState(); };
    addStateButton.onClick = [this] { addCompositionState(); };
    stateAdvanceModeButton.onClick = [this] { toggleSelectedStateAdvanceMode(); };
    stateAdvanceIntervalEditor.onReturnKey = [this] { applyStateAdvanceEditor(); };
    stateAdvanceIntervalEditor.onFocusLost = [this] { applyStateAdvanceEditor(); };
    stateTimeSignatureNumeratorEditor.onReturnKey = [this] { applyStateTimeSignatureEditors(); };
    stateTimeSignatureNumeratorEditor.onFocusLost = [this] { applyStateTimeSignatureEditors(); };
    stateTimeSignatureDenominatorEditor.onReturnKey = [this] { applyStateTimeSignatureEditors(); };
    stateTimeSignatureDenominatorEditor.onFocusLost = [this] { applyStateTimeSignatureEditors(); };

    refreshStateGraph();
}

void MainComponent::refreshStateGraph()
{
    std::vector<StateGraphComponent::StateView> views;
    std::vector<StateGraphComponent::TransitionView> transitions;
    std::vector<juce::String> stateTransitionCodes;

    {
        const std::lock_guard lock(gridRuntimeMutex);
        views.reserve(compositionStates.size());
        stateTransitionCodes.reserve(compositionStates.size());

        for (const auto& state : compositionStates)
        {
            views.push_back({ state.name,
                              static_cast<int>(state.grids.size()),
                              0,
                              state.bpm });
            stateTransitionCodes.push_back(state.transitionCode);
        }

        if (! views.empty())
            views[static_cast<std::size_t>(juce::jlimit(0, static_cast<int>(views.size()) - 1, activeStateIndex))].activeGrid = activeGridSlot;
    }

    const auto stateCount = static_cast<int>(views.size());

    for (int stateIndex = 0; stateIndex < static_cast<int>(stateTransitionCodes.size()); ++stateIndex)
    {
        const auto rules = parseTransitionRules(stateTransitionCodes[static_cast<std::size_t>(stateIndex)]);

        for (const auto& [fromState, toState] : rules.linear)
        {
            const auto fromIndex = fromState - 1;
            const auto toIndex = toState - 1;

            if (fromIndex >= 0 && fromIndex < stateCount && toIndex >= 0 && toIndex < stateCount)
                transitions.push_back({ fromIndex, toIndex, 1.0, false });
        }

        for (const auto& [fromState, choices] : rules.weighted)
        {
            const auto fromIndex = fromState - 1;

            if (fromIndex < 0 || fromIndex >= stateCount)
                continue;

            for (const auto& choice : choices)
            {
                const auto toIndex = choice.targetState - 1;

                if (toIndex >= 0 && toIndex < stateCount)
                    transitions.push_back({ fromIndex, toIndex, choice.chance, true });
            }
        }

        for (int cycleIndex = 0; cycleIndex < static_cast<int>(rules.cycle.size()); ++cycleIndex)
        {
            const auto fromIndex = rules.cycle[static_cast<std::size_t>(cycleIndex)] - 1;
            const auto toIndex = rules.cycle[static_cast<std::size_t>((cycleIndex + 1) % static_cast<int>(rules.cycle.size()))] - 1;

            if (fromIndex >= 0 && fromIndex < stateCount && toIndex >= 0 && toIndex < stateCount)
                transitions.push_back({ fromIndex, toIndex, 1.0, false });
        }

        for (const auto& [triggerName, choices] : rules.triggers)
        {
            juce::ignoreUnused(triggerName);

            for (const auto& choice : choices)
            {
                const auto toIndex = choice.targetState - 1;

                if (stateIndex >= 0 && stateIndex < stateCount && toIndex >= 0 && toIndex < stateCount)
                    transitions.push_back({ stateIndex, toIndex, choice.chance, true });
            }
        }
    }

    stateGraph.setStates(std::move(views), activeStateIndex, transportEngine.isPlaying());
    stateGraph.setTransitions(std::move(transitions));
}

juce::String MainComponent::getStateAdvanceModeText(const CompositionState::AdvanceMode mode) const
{
    switch (mode)
    {
        case CompositionState::AdvanceMode::manual:  return "MANUAL";
        case CompositionState::AdvanceMode::beats:   return "BEATS";
        case CompositionState::AdvanceMode::bars:    return "BARS";
        case CompositionState::AdvanceMode::trigger: return "TRIGGER";
    }

    return "MANUAL";
}

static juce::String compactStateAdvanceModeText(const juce::String& text)
{
    if (text == "BEATS")   return "BEAT";
    if (text == "BARS")    return "BAR";
    if (text == "TRIGGER") return "TRG";
    return "MAN";
}

void MainComponent::toggleSelectedStateAdvanceMode()
{
    CompositionState::AdvanceMode mode = CompositionState::AdvanceMode::manual;

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (compositionStates.empty())
            return;

        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

        switch (state.advanceMode)
        {
            case CompositionState::AdvanceMode::manual:  state.advanceMode = CompositionState::AdvanceMode::beats; break;
            case CompositionState::AdvanceMode::beats:   state.advanceMode = CompositionState::AdvanceMode::bars; break;
            case CompositionState::AdvanceMode::bars:    state.advanceMode = CompositionState::AdvanceMode::trigger; break;
            case CompositionState::AdvanceMode::trigger: state.advanceMode = CompositionState::AdvanceMode::manual; break;
        }

        mode = state.advanceMode;
        state.advanceInterval = juce::jlimit(1, 999, state.advanceInterval);
        activeStateEntryFrame = lastTransportFrame;
    }

    updateStateAdvanceControls();
    refreshArrangementView();
    statusLog.append("State advance: " + getStateAdvanceModeText(mode));
    repaint();
}

void MainComponent::applyStateAdvanceEditor()
{
    const auto interval = juce::jlimit(1, 999, stateAdvanceIntervalEditor.getText().getIntValue());

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (compositionStates.empty())
            return;

        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        compositionStates[static_cast<std::size_t>(activeStateIndex)].advanceInterval = interval;
        activeStateEntryFrame = lastTransportFrame;
    }

    updateStateAdvanceControls();
    refreshArrangementView();
    statusLog.append("State advance interval: " + juce::String(interval));
    repaint();
}

void MainComponent::applyStateTimeSignatureEditors()
{
    const auto numerator = juce::jlimit(1, 32, stateTimeSignatureNumeratorEditor.getText().getIntValue());
    const auto denominator = juce::jlimit(1, 32, stateTimeSignatureDenominatorEditor.getText().getIntValue());

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (compositionStates.empty())
            return;

        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];
        state.timeSignatureNumerator = numerator;
        state.timeSignatureDenominator = denominator;
    }

    updateStateAdvanceControls();
    refreshStateGraph();
    refreshArrangementView();
    statusLog.append("State " + juce::String(activeStateIndex + 1)
                     + " time signature " + juce::String(numerator)
                     + "/" + juce::String(denominator));
    repaint();
}

void MainComponent::updateStateAdvanceControls()
{
    CompositionState::AdvanceMode mode = CompositionState::AdvanceMode::manual;
    int interval = 4;
    int numerator = 4;
    int denominator = 4;
    bool hasState = false;

    {
        const std::lock_guard lock(gridRuntimeMutex);
        hasState = ! compositionStates.empty();

        if (hasState)
        {
            const auto index = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
            const auto& state = compositionStates[static_cast<std::size_t>(index)];
            mode = state.advanceMode;
            interval = state.advanceInterval;
            numerator = state.timeSignatureNumerator;
            denominator = state.timeSignatureDenominator;
        }
    }

    stateAdvanceModeButton.setEnabled(hasState);
    stateAdvanceModeButton.setButtonText(compactStateAdvanceModeText(getStateAdvanceModeText(mode)));
    stateAdvanceIntervalEditor.setEnabled(hasState
                                          && mode != CompositionState::AdvanceMode::manual
                                          && mode != CompositionState::AdvanceMode::trigger);
    stateAdvanceIntervalEditor.setText(juce::String(interval), juce::dontSendNotification);
    forceBlackEditorText(stateAdvanceIntervalEditor);
    stateTimeSignatureNumeratorEditor.setEnabled(hasState);
    stateTimeSignatureDenominatorEditor.setEnabled(hasState);
    stateTimeSignatureNumeratorEditor.setText(juce::String(numerator), juce::dontSendNotification);
    stateTimeSignatureDenominatorEditor.setText(juce::String(denominator), juce::dontSendNotification);
    forceBlackEditorText(stateTimeSignatureNumeratorEditor);
    forceBlackEditorText(stateTimeSignatureDenominatorEditor);
}

void MainComponent::styleTransportControls()
{
    const auto background = juce::Colour::fromRGB(48, 49, 50);
    const auto text = lewittInk();
    const auto outline = lewittLine().withAlpha(0.62f);

    for (auto* button : { &playPauseButton, &stopButton, &resetButton })
    {
        button->setColour(juce::TextButton::buttonColourId, background);
        button->setColour(juce::TextButton::buttonOnColourId, lewittBlue().withAlpha(0.86f));
        button->setColour(juce::TextButton::textColourOffId, text);
        button->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        button->setColour(juce::ComboBox::outlineColourId, outline);
    }

    bpmEditor.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    bpmEditor.setJustification(juce::Justification::centred);
    bpmEditor.setColour(juce::TextEditor::backgroundColourId, background);
    bpmEditor.setColour(juce::TextEditor::outlineColourId, outline);
    bpmEditor.setColour(juce::TextEditor::focusedOutlineColourId, text);
    bpmEditor.setColour(juce::TextEditor::highlightColourId, lewittBlue().withAlpha(0.35f));
    forceBlackEditorText(bpmEditor);

    for (auto* label : { &bpmLabel })
    {
        label->setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));
        label->setJustificationType(juce::Justification::centredLeft);
        label->setColour(juce::Label::textColourId, text);
    }
}

void MainComponent::updateTransportControls()
{
    playPauseButton.setButtonText(transportEngine.isPlaying() ? "PAUS" : "PLAY");
    stopButton.setButtonText("STOP");
    resetButton.setButtonText("RST");
    bpmEditor.setText(juce::String(transportEngine.getBpm(), 1), juce::dontSendNotification);
    forceBlackEditorText(bpmEditor);
}

void MainComponent::configureGridSlotControls()
{
    addAndMakeVisible(gridSlotLabel);
    addAndMakeVisible(previousGridButton);
    addAndMakeVisible(nextGridButton);
    addAndMakeVisible(addGridButton);
    addAndMakeVisible(gridRatioLabel);
    addAndMakeVisible(gridRatioEditor);
    addAndMakeVisible(phaseModeButton);
    addAndMakeVisible(phaseOffsetEditor);
    addAndMakeVisible(laneKindButton);
    addAndMakeVisible(gridSizeLabel);
    addAndMakeVisible(gridColumnsEditor);
    addAndMakeVisible(gridSizeSeparatorLabel);
    addAndMakeVisible(gridRowsEditor);

    for (int index = 0; index < static_cast<int>(gridTabButtons.size()); ++index)
    {
        auto& button = gridTabButtons[static_cast<std::size_t>(index)];
        addAndMakeVisible(button);
        button.setButtonText(juce::String(index + 1).paddedLeft('0', 2));
        button.setTooltip("Select grid " + juce::String(index + 1));
        button.onClick = [this, index] { switchToGridSlot(index); };
    }

    previousGridButton.setButtonText("<");
    nextGridButton.setButtonText(">");
    addGridButton.setButtonText("+");
    gridRatioLabel.setText("1:", juce::dontSendNotification);
    gridSizeLabel.setText("SIZE", juce::dontSendNotification);
    gridSizeSeparatorLabel.setText("x", juce::dontSendNotification);

    previousGridButton.setTooltip("Previous grid");
    nextGridButton.setTooltip("Next grid");
    addGridButton.setTooltip("Add grid");
    gridRatioEditor.setTooltip("Selected grid tempo ratio denominator");
    phaseModeButton.setTooltip("Selected lane phase mode");
    phaseOffsetEditor.setTooltip("Selected lane phase offset, 0-360 degrees");
    laneKindButton.setTooltip("Swap selected lane between grid and SuperCollider code");
    gridColumnsEditor.setTooltip("Selected grid columns, 1-64");
    gridRowsEditor.setTooltip("Selected grid rows, 1-32");

    previousGridButton.onClick = [this] { previousGridSlot(); };
    nextGridButton.onClick = [this] { nextGridSlot(); };
    addGridButton.onClick = [this] { addGridSlot(); };
    gridRatioEditor.onReturnKey = [this] { applyGridTimingEditors(); };
    gridRatioEditor.onFocusLost = [this] { applyGridTimingEditors(); };
    phaseModeButton.onClick = [this] { toggleSelectedGridPhaseMode(); };
    phaseOffsetEditor.onReturnKey = [this] { applyGridTimingEditors(); };
    phaseOffsetEditor.onFocusLost = [this] { applyGridTimingEditors(); };
    laneKindButton.onClick = [this] { toggleSelectedLaneKind(); };
    gridColumnsEditor.onReturnKey = [this] { applyGridSizeEditors(); };
    gridRowsEditor.onReturnKey = [this] { applyGridSizeEditors(); };
    gridColumnsEditor.onFocusLost = [this] { applyGridSizeEditors(); };
    gridRowsEditor.onFocusLost = [this] { applyGridSizeEditors(); };

    styleGridSlotControls();
    updateGridSlotControls();
}

void MainComponent::configureMixerView()
{
    addAndMakeVisible(mixerViewport);
    mixerViewport.setViewedComponent(&mixerContent, false);
    mixerViewport.setScrollBarsShown(false, true);
    mixerViewport.setVisible(false);

    mixerContent.addAndMakeVisible(mixerLabel);
    mixerLabel.setText("MIXER", juce::dontSendNotification);
    mixerLabel.setVisible(false);

    for (int index = 0; index < maximumMixerChannels; ++index)
    {
        auto& label = mixerChannelLabels[static_cast<std::size_t>(index)];
        auto& level = mixerLevelSliders[static_cast<std::size_t>(index)];
        auto& pan = mixerPanSliders[static_cast<std::size_t>(index)];
        auto& mute = mixerMuteButtons[static_cast<std::size_t>(index)];
        auto& solo = mixerSoloButtons[static_cast<std::size_t>(index)];

        mixerContent.addAndMakeVisible(label);
        mixerContent.addAndMakeVisible(level);
        mixerContent.addAndMakeVisible(pan);
        mixerContent.addAndMakeVisible(mute);
        mixerContent.addAndMakeVisible(solo);

        label.setJustificationType(juce::Justification::centred);
        level.setSliderStyle(juce::Slider::LinearVertical);
        level.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        level.setRange(0.0, 1.25, 0.01);
        level.setValue(1.0, juce::dontSendNotification);

        pan.setSliderStyle(juce::Slider::LinearHorizontal);
        pan.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        pan.setRange(-1.0, 1.0, 0.01);
        pan.setValue(0.0, juce::dontSendNotification);
        mute.setButtonText("M");
        solo.setButtonText("S");
        mute.setClickingTogglesState(true);
        solo.setClickingTogglesState(true);

        level.onValueChange = [this, index]
        {
            if (index == masterMixerControlIndex)
                applyMasterLevel();
            else
                applyMixerControl(index / maximumGridsPerState, index % maximumGridsPerState, index, false);
        };

        pan.onValueChange = [this, index]
        {
            if (index != masterMixerControlIndex)
                applyMixerControl(index / maximumGridsPerState, index % maximumGridsPerState, index, false);
        };
    }

    styleMixerControls();
    refreshMixerView();
}

void MainComponent::styleMixerControls()
{
    const auto mixerInk = juce::Colour::fromRGB(238, 239, 240);
    const auto mixerPaper = juce::Colour::fromRGB(42, 43, 42);
    const auto mixerLine = juce::Colour::fromRGB(20, 21, 22);
    const auto mixerBlue = juce::Colour::fromRGB(95, 192, 239);

    mixerLabel.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));
    mixerLabel.setColour(juce::Label::textColourId, mixerInk);

    for (int index = 0; index < maximumMixerChannels; ++index)
    {
        auto& label = mixerChannelLabels[static_cast<std::size_t>(index)];
        auto& level = mixerLevelSliders[static_cast<std::size_t>(index)];
        auto& pan = mixerPanSliders[static_cast<std::size_t>(index)];
        auto& mute = mixerMuteButtons[static_cast<std::size_t>(index)];
        auto& solo = mixerSoloButtons[static_cast<std::size_t>(index)];

        label.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::bold));
        label.setColour(juce::Label::textColourId, mixerInk);
        level.setColour(juce::Slider::backgroundColourId, mixerLine.withAlpha(0.32f));
        level.setColour(juce::Slider::trackColourId, mixerBlue);
        level.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(220, 222, 224));
        level.setColour(juce::Slider::textBoxTextColourId, mixerInk);
        level.setColour(juce::Slider::textBoxBackgroundColourId, mixerPaper);
        level.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour::fromRGB(120, 123, 126).withAlpha(0.72f));
        pan.setColour(juce::Slider::backgroundColourId, mixerLine.withAlpha(0.28f));
        pan.setColour(juce::Slider::trackColourId, mixerBlue);
        pan.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(220, 222, 224));

        for (auto* button : { &mute, &solo })
        {
            button->setColour(juce::TextButton::buttonColourId, mixerPaper);
            button->setColour(juce::TextButton::buttonOnColourId, mixerBlue);
            button->setColour(juce::TextButton::textColourOffId, mixerInk);
            button->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
            button->setColour(juce::ComboBox::outlineColourId, mixerLine);
        }
    }
}

void MainComponent::toggleMixerView()
{
    mixerViewVisible = ! mixerViewVisible;
    activeSplitterDrag = SplitterDrag::none;
    setMouseCursor(juce::MouseCursor::NormalCursor);

    if (mixerViewVisible)
        arrangementViewVisible = false;
    else
        showActiveLane();

    refreshMixerView();
    resized();
    repaint();
}

void MainComponent::refreshMixerView()
{
    struct Channel
    {
        int state = -1;
        int lane = -1;
        juce::String label;
        juce::String output;
        juce::Colour colour;
        float level = 1.0f;
        float pan = 0.0f;
        bool master = false;
    };

    static constexpr std::array<std::uint32_t, 10> channelColours {
        0xff7b5a28, 0xffd6d33d, 0xff416fc8, 0xffc05a2d, 0xff9b33a8,
        0xffb47b2d, 0xff4da24a, 0xff8c78c8, 0xff3d8b78, 0xff6c7f2d
    };

    std::vector<Channel> channels;
    {
        const std::lock_guard lock(gridRuntimeMutex);

        for (int stateIndex = 0; stateIndex < static_cast<int>(compositionStates.size()); ++stateIndex)
        {
            const auto& state = compositionStates[static_cast<std::size_t>(stateIndex)];

            for (int laneIndex = 0; laneIndex < static_cast<int>(state.grids.size()); ++laneIndex)
            {
                const auto& lane = state.grids[static_cast<std::size_t>(laneIndex)];
                const auto colourIndex = static_cast<std::size_t>(stateIndex * 3 + laneIndex) % channelColours.size();
                channels.push_back({ stateIndex,
                                     laneIndex,
                                     "S" + juce::String(stateIndex + 1).paddedLeft('0', 2)
                                         + " L" + juce::String(laneIndex + 1).paddedLeft('0', 2)
                                         + " " + (lane.kind == CompositionGrid::Kind::grid ? "G" : "SC"),
                                     lane.kind == CompositionGrid::Kind::grid ? "GRID" : "SYNTH",
                                     juce::Colour(channelColours[colourIndex]),
                                     lane.mixerLevel,
                                     lane.mixerPan,
                                     false });
            }
        }
    }

    channels.push_back({ -1, -1, "MASTER", "ST OUT", juce::Colour::fromRGB(86, 64, 178), masterLevel, 0.0f, true });
    masterMixerControlIndex = static_cast<int>(channels.size()) - 1;

    const auto channelCount = juce::jmax(1, static_cast<int>(channels.size()));
    const auto stripWidth = 58;
    const auto contentHeight = juce::jmax(520, mixerViewport.getHeight());
    const auto contentWidth = juce::jmax(20 + channelCount * stripWidth, mixerViewport.getWidth());
    mixerContent.setBounds(0, 0, contentWidth, contentHeight);
    std::vector<MixerContentComponent::Strip> strips;
    strips.reserve(channels.size());
    for (const auto& channel : channels)
    {
        const auto meterIndex = channel.master
                                    ? maximumMixerChannels - 1
                                    : channel.state * maximumGridsPerState + channel.lane;
        const auto meter = meterIndex >= 0 && meterIndex < maximumMixerChannels
                               ? mixerMeterDisplay[static_cast<std::size_t>(meterIndex)]
                               : 0.0f;
        strips.push_back({ channel.label, channel.output, channel.colour, channel.master, meter });
    }
    mixerContent.setStrips(std::move(strips), stripWidth, contentHeight);
    mixerLabel.setBounds({});

    for (int index = 0; index < maximumMixerChannels; ++index)
    {
        const auto visible = index < static_cast<int>(channels.size());
        auto& label = mixerChannelLabels[static_cast<std::size_t>(index)];
        auto& level = mixerLevelSliders[static_cast<std::size_t>(index)];
        auto& pan = mixerPanSliders[static_cast<std::size_t>(index)];
        auto& mute = mixerMuteButtons[static_cast<std::size_t>(index)];
        auto& solo = mixerSoloButtons[static_cast<std::size_t>(index)];

        label.setVisible(false);
        level.setVisible(visible);
        pan.setVisible(visible && ! channels[static_cast<std::size_t>(index)].master);
        mute.setVisible(false);
        solo.setVisible(false);

        if (! visible)
            continue;

        const auto& channel = channels[static_cast<std::size_t>(index)];
        const auto x = 10 + index * stripWidth;
        const auto stripBounds = juce::Rectangle<int>(x, 10, stripWidth - 8, juce::jmax(380, contentHeight - 20));
        const auto panY = stripBounds.getY() + 87;
        const auto faderTop = stripBounds.getY() + (channel.master ? 88 : 124);
        const auto faderBottom = stripBounds.getBottom() - 74;
        pan.setBounds(stripBounds.getX() + 6, panY, stripBounds.getWidth() - 12, 18);
        level.setBounds(stripBounds.getCentreX() - 14, faderTop, 28, juce::jmax(180, faderBottom - faderTop));
        mute.setBounds({});
        solo.setBounds({});
        level.setColour(juce::Slider::trackColourId, channel.colour.withAlpha(0.76f));
        pan.setColour(juce::Slider::trackColourId, channel.colour.withAlpha(0.78f));
        level.setValue(channel.level, juce::dontSendNotification);
        pan.setValue(channel.pan, juce::dontSendNotification);

        if (channel.master)
        {
            level.onValueChange = [this] { applyMasterLevel(); };
        }
        else
        {
            level.onValueChange = [this, stateIndex = channel.state, laneIndex = channel.lane, index]
            {
                applyMixerControl(stateIndex, laneIndex, index, false);
            };
            level.onDragEnd = [this, stateIndex = channel.state, laneIndex = channel.lane, index]
            {
                applyMixerControl(stateIndex, laneIndex, index, true);
            };
            pan.onValueChange = [this, stateIndex = channel.state, laneIndex = channel.lane, index]
            {
                applyMixerControl(stateIndex, laneIndex, index, false);
            };
            pan.onDragEnd = [this, stateIndex = channel.state, laneIndex = channel.lane, index]
            {
                applyMixerControl(stateIndex, laneIndex, index, true);
            };
        }
    }
}

void MainComponent::refreshMixerMeters()
{
    std::vector<float> meters;

    {
        const std::lock_guard lock(gridRuntimeMutex);

        for (int stateIndex = 0; stateIndex < static_cast<int>(compositionStates.size()); ++stateIndex)
        {
            const auto& state = compositionStates[static_cast<std::size_t>(stateIndex)];

            for (int laneIndex = 0; laneIndex < static_cast<int>(state.grids.size()); ++laneIndex)
            {
                const auto meterIndex = stateIndex * maximumGridsPerState + laneIndex;
                meters.push_back(meterIndex >= 0 && meterIndex < maximumMixerChannels
                                     ? mixerMeterDisplay[static_cast<std::size_t>(meterIndex)]
                                     : 0.0f);
            }
        }
    }

    meters.push_back(mixerMeterDisplay[static_cast<std::size_t>(maximumMixerChannels - 1)]);
    mixerContent.setMeters(meters);
}

void MainComponent::configureArrangementView()
{
    addAndMakeVisible(arrangementViewport);
    arrangementViewport.setViewedComponent(&arrangementContent, false);
    arrangementViewport.setScrollBarsShown(false, true);
    arrangementViewport.setVisible(false);
    refreshArrangementView();
}

void MainComponent::toggleArrangementView()
{
    arrangementViewVisible = ! arrangementViewVisible;
    activeSplitterDrag = SplitterDrag::none;
    setMouseCursor(juce::MouseCursor::NormalCursor);

    if (arrangementViewVisible)
        mixerViewVisible = false;
    else
        showActiveLane();

    refreshArrangementView();
    resized();
    repaint();
}

void MainComponent::refreshArrangementView()
{
    static constexpr std::array<std::uint32_t, 10> stateColours {
        0xffd6aa38, 0xff5aa8d6, 0xff7ab77c, 0xffc27b55, 0xffb75c85,
        0xff8a72cf, 0xffd65d42, 0xff62b59c, 0xffc4c64d, 0xff6f8bd6
    };

    static constexpr std::array<std::uint32_t, 8> laneColours {
        0xffe44f3a, 0xfff4cf3e, 0xff326cff, 0xff20b95e,
        0xffd87034, 0xff9b59d0, 0xff53bfd0, 0xfff06fa4
    };

    std::vector<ArrangementContentComponent::State> states;
    std::vector<juce::String> transitionCodes;
    int selectedIndex = 0;

    {
        const std::lock_guard lock(gridRuntimeMutex);
        states.reserve(compositionStates.size());
        transitionCodes.reserve(compositionStates.size());
        selectedIndex = activeStateIndex;

        for (int stateIndex = 0; stateIndex < static_cast<int>(compositionStates.size()); ++stateIndex)
        {
            const auto& state = compositionStates[static_cast<std::size_t>(stateIndex)];
            ArrangementContentComponent::State view;
            view.name = state.name;
            view.laneCount = static_cast<int>(state.grids.size());
            view.bars = state.advanceMode == CompositionState::AdvanceMode::bars
                            ? juce::jmax(1, state.advanceInterval)
                            : 1;
            view.numerator = state.timeSignatureNumerator;
            view.denominator = state.timeSignatureDenominator;
            view.bpm = state.bpm;
            view.selected = stateIndex == activeStateIndex;
            view.colour = juce::Colour(stateColours[static_cast<std::size_t>(stateIndex) % stateColours.size()]);
            view.laneColours.reserve(state.grids.size());

            for (int laneIndex = 0; laneIndex < static_cast<int>(state.grids.size()); ++laneIndex)
            {
                const auto& lane = state.grids[static_cast<std::size_t>(laneIndex)];
                auto colour = juce::Colour(laneColours[static_cast<std::size_t>(laneIndex) % laneColours.size()]);
                if (lane.kind == CompositionGrid::Kind::supercollider)
                    colour = colour.interpolatedWith(juce::Colours::white, 0.18f);
                view.laneColours.push_back(colour);
            }

            states.push_back(std::move(view));
            transitionCodes.push_back(state.transitionCode);
        }
    }

    std::vector<ArrangementContentComponent::Edge> edges;
    const auto stateCount = static_cast<int>(states.size());
    for (int stateIndex = 0; stateIndex < static_cast<int>(transitionCodes.size()); ++stateIndex)
    {
        const auto rules = parseTransitionRules(transitionCodes[static_cast<std::size_t>(stateIndex)]);

        for (const auto& [fromState, toState] : rules.linear)
        {
            const auto fromIndex = fromState - 1;
            const auto toIndex = toState - 1;

            if (fromIndex >= 0 && fromIndex < stateCount && toIndex >= 0 && toIndex < stateCount)
                edges.push_back({ fromIndex, toIndex, 1.0, false });
        }

        for (const auto& [fromState, choices] : rules.weighted)
        {
            const auto fromIndex = fromState - 1;

            if (fromIndex < 0 || fromIndex >= stateCount)
                continue;

            for (const auto& choice : choices)
            {
                const auto toIndex = choice.targetState - 1;

                if (toIndex >= 0 && toIndex < stateCount)
                    edges.push_back({ fromIndex, toIndex, choice.chance, true });
            }
        }

        for (int cycleIndex = 0; cycleIndex < static_cast<int>(rules.cycle.size()); ++cycleIndex)
        {
            const auto fromIndex = rules.cycle[static_cast<std::size_t>(cycleIndex)] - 1;
            const auto toIndex = rules.cycle[static_cast<std::size_t>((cycleIndex + 1) % static_cast<int>(rules.cycle.size()))] - 1;

            if (fromIndex >= 0 && fromIndex < stateCount && toIndex >= 0 && toIndex < stateCount)
                edges.push_back({ fromIndex, toIndex, 1.0, false });
        }

        for (const auto& [triggerName, choices] : rules.triggers)
        {
            juce::ignoreUnused(triggerName);

            for (const auto& choice : choices)
            {
                const auto toIndex = choice.targetState - 1;

                if (stateIndex >= 0 && stateIndex < stateCount && toIndex >= 0 && toIndex < stateCount)
                    edges.push_back({ stateIndex, toIndex, choice.chance, true });
            }
        }
    }

    const auto contentWidth = juce::jmax(arrangementViewport.getWidth(), 920);
    const auto contentHeight = juce::jmax(arrangementViewport.getHeight(), 560);
    arrangementContent.setBounds(0, 0, contentWidth, contentHeight);
    arrangementContent.setArrangement(std::move(states), std::move(edges), selectedIndex);
}

void MainComponent::initialiseDefaultInstrumentLayer()
{
    const auto defaults = EmbeddedScAudioEngine::getDefaultChannelInstruments();
    for (int channel = 0; channel < instrumentChannelCount; ++channel)
        channelInstrumentMap[static_cast<std::size_t>(channel)] = channel < defaults.size() ? defaults[channel] : "tone";

    userInstruments.clear();
    defaultInstruments.clear();
    for (const auto& name : EmbeddedScAudioEngine::getDefaultSynthDefNames())
        defaultInstruments.push_back({ name, EmbeddedScAudioEngine::getDefaultSynthDefSource(name) });

    userInstruments.push_back({ "gc_user_tone", createDefaultUserInstrumentCode("gc_user_tone") });
    selectedDefaultInstrumentIndex = defaultInstruments.empty() ? -1 : 0;
    selectedUserInstrumentIndex = 0;
    selectedInstrumentLaneReferenceIndex = -1;
}

juce::String MainComponent::createDefaultUserInstrumentCode(const juce::String& name) const
{
    const auto safeName = name.trim().isNotEmpty() ? name.trim() : "gc_user_tone";
    return "SynthDef(\\"
        + safeName
        + R"SC(, { |out = 0, pitch = 60, amp = 0.18, sustain = 0.45, pan = 0|
    var freq = pitch.midicps;
    var env = EnvGen.kr(Env.perc(0.008, sustain.max(0.04), curve: -3), doneAction: 2);
    var sig = SinOsc.ar(freq) * env * amp;
    Out.ar(out, Pan2.ar(sig, pan));
});
)SC";
}

void MainComponent::configureInstrumentView()
{
    addAndMakeVisible(instrumentView);
    instrumentView.setVisible(false);
    instrumentView.setOpaque(false);

    for (auto* component : { static_cast<juce::Component*>(&instrumentViewTitleLabel),
                             static_cast<juce::Component*>(&instrumentSelector),
                             static_cast<juce::Component*>(&instrumentNameEditor),
                             static_cast<juce::Component*>(&newInstrumentButton),
                             static_cast<juce::Component*>(&deleteInstrumentButton),
                             static_cast<juce::Component*>(&saveInstrumentButton),
                             static_cast<juce::Component*>(&compileInstrumentButton),
                             static_cast<juce::Component*>(&applyInstrumentMapButton),
                             static_cast<juce::Component*>(&instrumentCodeLabel),
                             static_cast<juce::Component*>(&instrumentMapLabel),
                             static_cast<juce::Component*>(&instrumentMapViewport),
                             static_cast<juce::Component*>(&instrumentCodeEditor) })
        instrumentView.addAndMakeVisible(component);

    instrumentMapViewport.setViewedComponent(&instrumentMapContent, false);
    instrumentMapViewport.setScrollBarsShown(true, false);
    instrumentMapViewport.setScrollBarThickness(8);

    for (int channel = 0; channel < instrumentChannelCount; ++channel)
    {
        instrumentMapContent.addAndMakeVisible(instrumentChannelLabels[static_cast<std::size_t>(channel)]);
        instrumentMapContent.addAndMakeVisible(instrumentChannelSelectors[static_cast<std::size_t>(channel)]);
    }

    instrumentViewTitleLabel.setText("Instruments", juce::dontSendNotification);
    instrumentCodeLabel.setText("SynthDef", juce::dontSendNotification);
    instrumentMapLabel.setText("Channel Map", juce::dontSendNotification);
    newInstrumentButton.setButtonText("New");
    deleteInstrumentButton.setButtonText("Delete");
    saveInstrumentButton.setButtonText("Save");
    compileInstrumentButton.setButtonText("Compile");
    applyInstrumentMapButton.setButtonText("Apply Map");
    instrumentNameEditor.setSelectAllWhenFocused(true);

    for (int channel = 0; channel < instrumentChannelCount; ++channel)
    {
        instrumentChannelLabels[static_cast<std::size_t>(channel)].setText("CH " + juce::String(channel).paddedLeft('0', 2), juce::dontSendNotification);
        instrumentChannelSelectors[static_cast<std::size_t>(channel)].setEditableText(true);
        instrumentChannelSelectors[static_cast<std::size_t>(channel)].setTextWhenNothingSelected("instrument");
        instrumentChannelSelectors[static_cast<std::size_t>(channel)].onChange = [this]
        {
            if (! updatingInstrumentView)
                applyChannelInstrumentEditors();
        };
    }

    instrumentSelector.onChange = [this]
    {
        if (updatingInstrumentView)
            return;

        selectInstrumentEditorTarget(instrumentSelector.getSelectedId());
    };
    instrumentNameEditor.onReturnKey = [this] { storeActiveInstrumentEditor(); refreshInstrumentView(); };
    instrumentNameEditor.onFocusLost = [this] { storeActiveInstrumentEditor(); refreshInstrumentView(); };
    newInstrumentButton.onClick = [this] { addUserInstrument(); };
    deleteInstrumentButton.onClick = [this] { deleteSelectedUserInstrument(); };
    saveInstrumentButton.onClick = [this] { saveSelectedInstrument(); };
    compileInstrumentButton.onClick = [this] { compileSelectedUserInstrument(); };
    applyInstrumentMapButton.onClick = [this] { applyChannelInstrumentEditors(); };

    instrumentCodeEditor.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    instrumentCodeEditor.setTabSize(4, true);
    instrumentCodeEditor.setScrollbarThickness(8);
    instrumentCodeDocumentListener.setCallback([this]
    {
        if (updatingInstrumentView)
            return;

        if (selectedInstrumentLaneReferenceIndex >= 0)
        {
            storeActiveLaneInstrument();
        }
        else if (selectedDefaultInstrumentIndex >= 0 && selectedDefaultInstrumentIndex < static_cast<int>(defaultInstruments.size()))
        {
            defaultInstruments[static_cast<std::size_t>(selectedDefaultInstrumentIndex)].code = instrumentCodeDocument.getAllContent();
            userInstrumentCodeDirty = true;
        }
        else if (selectedUserInstrumentIndex >= 0 && selectedUserInstrumentIndex < static_cast<int>(userInstruments.size()))
        {
            userInstruments[static_cast<std::size_t>(selectedUserInstrumentIndex)].code = instrumentCodeDocument.getAllContent();
            userInstrumentCodeDirty = true;
        }
    });
    instrumentCodeDocument.addListener(&instrumentCodeDocumentListener);

    styleInstrumentView();
    refreshInstrumentView();
}

void MainComponent::styleInstrumentView()
{
    const auto panel = juce::Colour::fromRGB(34, 35, 36);
    const auto field = juce::Colour::fromRGB(24, 25, 26);
    const auto text = lewittInk();
    const auto outline = lewittLine().withAlpha(0.62f);

    for (auto* label : { &instrumentViewTitleLabel, &instrumentCodeLabel, &instrumentMapLabel })
    {
        label->setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), label == &instrumentViewTitleLabel ? 20.0f : 13.0f, juce::Font::bold));
        label->setColour(juce::Label::textColourId, text);
    }

    for (auto& label : instrumentChannelLabels)
    {
        label.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));
        label.setJustificationType(juce::Justification::centredRight);
        label.setColour(juce::Label::textColourId, text.withAlpha(0.76f));
    }

    for (auto* button : { &newInstrumentButton, &deleteInstrumentButton, &saveInstrumentButton, &compileInstrumentButton, &applyInstrumentMapButton })
    {
        button->setColour(juce::TextButton::buttonColourId, panel);
        button->setColour(juce::TextButton::buttonOnColourId, lewittBlue().withAlpha(0.86f));
        button->setColour(juce::TextButton::textColourOffId, text);
        button->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    }

    instrumentSelector.setColour(juce::ComboBox::backgroundColourId, panel);
    instrumentSelector.setColour(juce::ComboBox::outlineColourId, outline);
    instrumentSelector.setColour(juce::ComboBox::textColourId, text);
    instrumentSelector.setColour(juce::PopupMenu::backgroundColourId, panel);
    instrumentSelector.setColour(juce::PopupMenu::textColourId, text);
    instrumentMapViewport.setColour(juce::ScrollBar::backgroundColourId, field);
    instrumentMapViewport.setColour(juce::ScrollBar::thumbColourId, lewittBlue());

    instrumentNameEditor.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    instrumentNameEditor.setColour(juce::TextEditor::backgroundColourId, field);
    instrumentNameEditor.setColour(juce::TextEditor::outlineColourId, outline);
    instrumentNameEditor.setColour(juce::TextEditor::focusedOutlineColourId, lewittBlue());
    instrumentNameEditor.setColour(juce::TextEditor::highlightColourId, lewittBlue().withAlpha(0.35f));
    forceBlackEditorText(instrumentNameEditor);

    for (auto& selector : instrumentChannelSelectors)
    {
        selector.setColour(juce::ComboBox::backgroundColourId, field);
        selector.setColour(juce::ComboBox::outlineColourId, outline);
        selector.setColour(juce::ComboBox::focusedOutlineColourId, lewittBlue());
        selector.setColour(juce::ComboBox::textColourId, text);
        selector.setColour(juce::ComboBox::arrowColourId, text.withAlpha(0.82f));
        selector.setColour(juce::PopupMenu::backgroundColourId, panel);
        selector.setColour(juce::PopupMenu::textColourId, text);
        selector.setColour(juce::PopupMenu::highlightedBackgroundColourId, lewittBlue().withAlpha(0.82f));
        selector.setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    }

    instrumentCodeEditor.setColour(juce::CodeEditorComponent::backgroundColourId, field);
    instrumentCodeEditor.setColour(juce::CodeEditorComponent::defaultTextColourId, text);
    instrumentCodeEditor.setColour(juce::CodeEditorComponent::highlightColourId, lewittBlue().withAlpha(0.34f));
    instrumentCodeEditor.setColour(juce::CodeEditorComponent::lineNumberBackgroundId, juce::Colour::fromRGB(31, 32, 33));
    instrumentCodeEditor.setColour(juce::CodeEditorComponent::lineNumberTextId, text.withAlpha(0.52f));
    instrumentCodeEditor.setColour(juce::ScrollBar::backgroundColourId, juce::Colour::fromRGB(35, 36, 37));
    instrumentCodeEditor.setColour(juce::ScrollBar::thumbColourId, lewittBlue());
    instrumentCodeEditor.setColourScheme(scCodeTokeniser.getDefaultColourScheme());
}

void MainComponent::refreshInstrumentView()
{
    updatingInstrumentView = true;

    instrumentSelector.clear(juce::dontSendNotification);
    instrumentLaneReferences.clear();

    if (! defaultInstruments.empty())
    {
        instrumentSelector.addSectionHeading("Default SynthDefs");

        for (int index = 0; index < static_cast<int>(defaultInstruments.size()); ++index)
            instrumentSelector.addItem(defaultInstruments[static_cast<std::size_t>(index)].name, instrumentDefaultComboBaseId + index);
    }

    if (! userInstruments.empty())
    {
        if (! defaultInstruments.empty())
            instrumentSelector.addSeparator();
        instrumentSelector.addSectionHeading("Custom SynthDefs");
    }

    for (int index = 0; index < static_cast<int>(userInstruments.size()); ++index)
        instrumentSelector.addItem(userInstruments[static_cast<std::size_t>(index)].name, index + 1);

    {
        const std::lock_guard lock(gridRuntimeMutex);

        for (int stateIndex = 0; stateIndex < static_cast<int>(compositionStates.size()); ++stateIndex)
        {
            const auto& state = compositionStates[static_cast<std::size_t>(stateIndex)];

            for (int laneIndex = 0; laneIndex < static_cast<int>(state.grids.size()); ++laneIndex)
            {
                const auto& lane = state.grids[static_cast<std::size_t>(laneIndex)];

                if (lane.kind != CompositionGrid::Kind::supercollider)
                    continue;

                const auto code = lane.scCode.isEmpty() ? createDefaultScLaneCode(stateIndex + 1, laneIndex + 1) : lane.scCode;
                const auto synthName = lane.scSynthName.isNotEmpty()
                                           ? lane.scSynthName
                                           : getSynthDefNameFromSource(code, stateIndex + 1, laneIndex + 1);
                const auto label = "S" + juce::String(stateIndex + 1).paddedLeft('0', 2)
                    + " L" + juce::String(laneIndex + 1).paddedLeft('0', 2)
                    + "  " + synthName;
                instrumentLaneReferences.push_back({ stateIndex, laneIndex, label, synthName });
            }
        }
    }

    if (! instrumentLaneReferences.empty())
    {
        instrumentSelector.addSeparator();
        instrumentSelector.addSectionHeading("Lane SynthDefs");

        for (int index = 0; index < static_cast<int>(instrumentLaneReferences.size()); ++index)
            instrumentSelector.addItem(instrumentLaneReferences[static_cast<std::size_t>(index)].label, instrumentLaneComboBaseId + index);
    }

    if (selectedUserInstrumentIndex < 0 && ! userInstruments.empty())
        selectedUserInstrumentIndex = 0;
    if (selectedDefaultInstrumentIndex < 0 && ! defaultInstruments.empty() && selectedInstrumentLaneReferenceIndex < 0)
        selectedDefaultInstrumentIndex = 0;
    selectedDefaultInstrumentIndex = juce::jlimit(-1, static_cast<int>(defaultInstruments.size()) - 1, selectedDefaultInstrumentIndex);
    selectedUserInstrumentIndex = juce::jlimit(-1, static_cast<int>(userInstruments.size()) - 1, selectedUserInstrumentIndex);

    if (selectedInstrumentLaneReferenceIndex >= static_cast<int>(instrumentLaneReferences.size()))
        selectedInstrumentLaneReferenceIndex = -1;

    if (selectedInstrumentLaneReferenceIndex >= 0)
    {
        const auto& reference = instrumentLaneReferences[static_cast<std::size_t>(selectedInstrumentLaneReferenceIndex)];
        juce::String code;
        juce::String synthName = reference.synthName;

        {
            const std::lock_guard lock(gridRuntimeMutex);
            if (reference.stateIndex >= 0 && reference.stateIndex < static_cast<int>(compositionStates.size()))
            {
                const auto& state = compositionStates[static_cast<std::size_t>(reference.stateIndex)];
                if (reference.laneIndex >= 0 && reference.laneIndex < static_cast<int>(state.grids.size()))
                {
                    const auto& lane = state.grids[static_cast<std::size_t>(reference.laneIndex)];
                    code = lane.scCode.isEmpty()
                               ? createDefaultScLaneCode(reference.stateIndex + 1, reference.laneIndex + 1)
                               : lane.scCode;
                    synthName = lane.scSynthName.isNotEmpty()
                                    ? lane.scSynthName
                                    : getSynthDefNameFromSource(code, reference.stateIndex + 1, reference.laneIndex + 1);
                }
            }
        }

        instrumentSelector.setSelectedId(instrumentLaneComboBaseId + selectedInstrumentLaneReferenceIndex, juce::dontSendNotification);
        instrumentNameEditor.setText(synthName, juce::dontSendNotification);
        instrumentNameEditor.setEnabled(false);
        instrumentCodeDocument.replaceAllContent(code);
        instrumentCodeLabel.setText("Lane SynthDef", juce::dontSendNotification);
    }
    else if (selectedDefaultInstrumentIndex >= 0)
    {
        const auto& instrument = defaultInstruments[static_cast<std::size_t>(selectedDefaultInstrumentIndex)];
        instrumentSelector.setSelectedId(instrumentDefaultComboBaseId + selectedDefaultInstrumentIndex, juce::dontSendNotification);
        instrumentNameEditor.setText(instrument.name, juce::dontSendNotification);
        instrumentNameEditor.setEnabled(false);
        instrumentCodeDocument.replaceAllContent(instrument.code);
        instrumentCodeLabel.setText("Default SynthDef", juce::dontSendNotification);
    }
    else if (selectedUserInstrumentIndex >= 0)
    {
        const auto& instrument = userInstruments[static_cast<std::size_t>(selectedUserInstrumentIndex)];
        instrumentSelector.setSelectedId(selectedUserInstrumentIndex + 1, juce::dontSendNotification);
        instrumentNameEditor.setText(instrument.name, juce::dontSendNotification);
        instrumentNameEditor.setEnabled(true);
        instrumentCodeDocument.replaceAllContent(instrument.code);
        instrumentCodeLabel.setText("Custom SynthDef", juce::dontSendNotification);
    }
    else
    {
        instrumentNameEditor.clear();
        instrumentNameEditor.setEnabled(false);
        instrumentCodeDocument.replaceAllContent({});
        instrumentCodeLabel.setText("SynthDef", juce::dontSendNotification);
    }

    juce::StringArray channelOptions;
    for (const auto& instrument : defaultInstruments)
        channelOptions.addIfNotAlreadyThere(instrument.name);
    for (const auto& instrument : userInstruments)
        channelOptions.addIfNotAlreadyThere(instrument.name);
    for (const auto& reference : instrumentLaneReferences)
        if (reference.synthName.isNotEmpty())
            channelOptions.addIfNotAlreadyThere(reference.synthName);

    channelOptions.sort(true);

    for (int channel = 0; channel < instrumentChannelCount; ++channel)
    {
        auto& selector = instrumentChannelSelectors[static_cast<std::size_t>(channel)];
        selector.clear(juce::dontSendNotification);

        int itemId = 1;
        for (const auto& option : channelOptions)
            selector.addItem(option, itemId++);

        selector.setText(channelInstrumentMap[static_cast<std::size_t>(channel)], juce::dontSendNotification);
    }

    deleteInstrumentButton.setEnabled(selectedInstrumentLaneReferenceIndex < 0 && selectedDefaultInstrumentIndex < 0 && selectedUserInstrumentIndex >= 0);
    saveInstrumentButton.setEnabled(selectedInstrumentLaneReferenceIndex >= 0 || selectedDefaultInstrumentIndex >= 0 || selectedUserInstrumentIndex >= 0);
    compileInstrumentButton.setEnabled(selectedInstrumentLaneReferenceIndex >= 0 || selectedDefaultInstrumentIndex >= 0 || selectedUserInstrumentIndex >= 0);
    updatingInstrumentView = false;
}

void MainComponent::storeActiveInstrumentEditor()
{
    if (selectedInstrumentLaneReferenceIndex >= 0)
        storeActiveLaneInstrument();
    else if (selectedDefaultInstrumentIndex >= 0)
        storeActiveDefaultInstrument();
    else
        storeActiveUserInstrument();
}

void MainComponent::storeActiveDefaultInstrument()
{
    if (selectedDefaultInstrumentIndex < 0 || selectedDefaultInstrumentIndex >= static_cast<int>(defaultInstruments.size()))
        return;

    auto& instrument = defaultInstruments[static_cast<std::size_t>(selectedDefaultInstrumentIndex)];
    instrument.code = instrumentCodeDocument.getAllContent();
}

void MainComponent::storeActiveUserInstrument()
{
    if (selectedUserInstrumentIndex < 0 || selectedUserInstrumentIndex >= static_cast<int>(userInstruments.size()))
        return;

    auto& instrument = userInstruments[static_cast<std::size_t>(selectedUserInstrumentIndex)];
    auto name = instrumentNameEditor.getText().retainCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_").trim();
    if (name.isEmpty())
        name = "gc_user_" + juce::String(selectedUserInstrumentIndex + 1).paddedLeft('0', 2);

    const auto oldName = instrument.name;
    auto code = instrumentCodeDocument.getAllContent();
    if (oldName.isNotEmpty() && name != oldName)
        code = code.replace("SynthDef(\\" + oldName, "SynthDef(\\" + name);

    instrument.name = name;
    instrument.code = code;
}

void MainComponent::storeActiveLaneInstrument()
{
    if (selectedInstrumentLaneReferenceIndex < 0 || selectedInstrumentLaneReferenceIndex >= static_cast<int>(instrumentLaneReferences.size()))
        return;

    const auto reference = instrumentLaneReferences[static_cast<std::size_t>(selectedInstrumentLaneReferenceIndex)];
    const auto code = instrumentCodeDocument.getAllContent();
    const auto synthDefs = extractSynthDefs(code, reference.stateIndex + 1, reference.laneIndex + 1);

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (reference.stateIndex < 0 || reference.stateIndex >= static_cast<int>(compositionStates.size()))
            return;

        auto& state = compositionStates[static_cast<std::size_t>(reference.stateIndex)];

        if (reference.laneIndex < 0 || reference.laneIndex >= static_cast<int>(state.grids.size()))
            return;

        auto& lane = state.grids[static_cast<std::size_t>(reference.laneIndex)];
        if (lane.kind != CompositionGrid::Kind::supercollider)
            return;

        lane.scCode = code;
        lane.scSynthName = synthDefs.front().name;
        lane.scCodeDirty = true;
    }

    if (reference.stateIndex == activeStateIndex && reference.laneIndex == activeGridSlot)
    {
        updatingLaneCodeEditor = true;
        laneScCodeDocument.replaceAllContent(code);
        updatingLaneCodeEditor = false;
    }
}

void MainComponent::selectInstrumentEditorTarget(const int comboId)
{
    storeActiveInstrumentEditor();

    if (comboId >= instrumentLaneComboBaseId)
    {
        selectedInstrumentLaneReferenceIndex = juce::jlimit(-1,
                                                            static_cast<int>(instrumentLaneReferences.size()) - 1,
                                                            comboId - instrumentLaneComboBaseId);
        selectedDefaultInstrumentIndex = -1;
        selectedUserInstrumentIndex = juce::jlimit(-1,
                                                   static_cast<int>(userInstruments.size()) - 1,
                                                   selectedUserInstrumentIndex);
    }
    else if (comboId >= instrumentDefaultComboBaseId)
    {
        selectedInstrumentLaneReferenceIndex = -1;
        selectedDefaultInstrumentIndex = juce::jlimit(-1,
                                                      static_cast<int>(defaultInstruments.size()) - 1,
                                                      comboId - instrumentDefaultComboBaseId);
        selectedUserInstrumentIndex = juce::jlimit(-1,
                                                   static_cast<int>(userInstruments.size()) - 1,
                                                   selectedUserInstrumentIndex);
    }
    else
    {
        selectedInstrumentLaneReferenceIndex = -1;
        selectedDefaultInstrumentIndex = -1;
        selectedUserInstrumentIndex = juce::jlimit(-1, static_cast<int>(userInstruments.size()) - 1, comboId - 1);
    }

    userInstrumentCodeDirty = false;
    refreshInstrumentView();
}

void MainComponent::selectUserInstrument(const int index)
{
    storeActiveInstrumentEditor();
    selectedInstrumentLaneReferenceIndex = -1;
    selectedDefaultInstrumentIndex = -1;
    selectedUserInstrumentIndex = juce::jlimit(-1, static_cast<int>(userInstruments.size()) - 1, index);
    userInstrumentCodeDirty = false;
    refreshInstrumentView();
}

void MainComponent::addUserInstrument()
{
    storeActiveInstrumentEditor();
    const auto index = static_cast<int>(userInstruments.size()) + 1;
    const auto name = "gc_user_" + juce::String(index).paddedLeft('0', 2);
    userInstruments.push_back({ name, createDefaultUserInstrumentCode(name) });
    selectedInstrumentLaneReferenceIndex = -1;
    selectedDefaultInstrumentIndex = -1;
    selectedUserInstrumentIndex = static_cast<int>(userInstruments.size()) - 1;
    refreshInstrumentView();
}

void MainComponent::deleteSelectedUserInstrument()
{
    if (selectedUserInstrumentIndex < 0 || selectedUserInstrumentIndex >= static_cast<int>(userInstruments.size()))
        return;

    userInstruments.erase(userInstruments.begin() + selectedUserInstrumentIndex);
    selectedUserInstrumentIndex = juce::jmin(selectedUserInstrumentIndex, static_cast<int>(userInstruments.size()) - 1);
    refreshInstrumentView();
}

void MainComponent::saveSelectedInstrument()
{
    storeActiveInstrumentEditor();
    refreshInstrumentView();
    statusLog.append(selectedInstrumentLaneReferenceIndex >= 0 ? "Saved lane SynthDef"
                                                               : selectedDefaultInstrumentIndex >= 0 ? "Saved default SynthDef"
                                                                                                     : "Saved instrument SynthDef");
    repaint();
}

void MainComponent::compileSelectedUserInstrument()
{
    storeActiveInstrumentEditor();

    if (! embeddedScAudio.isReady())
    {
        statusLog.append("Instrument compile skipped: audio is not ready");
        repaint();
        return;
    }

    juce::String nameForLog;
    juce::String code;
    int seedIndex = selectedUserInstrumentIndex + 1;

    if (selectedInstrumentLaneReferenceIndex >= 0 && selectedInstrumentLaneReferenceIndex < static_cast<int>(instrumentLaneReferences.size()))
    {
        const auto& reference = instrumentLaneReferences[static_cast<std::size_t>(selectedInstrumentLaneReferenceIndex)];
        nameForLog = reference.label;
        code = instrumentCodeDocument.getAllContent();
        seedIndex = reference.laneIndex + 1;
    }
    else if (selectedDefaultInstrumentIndex >= 0 && selectedDefaultInstrumentIndex < static_cast<int>(defaultInstruments.size()))
    {
        const auto& instrument = defaultInstruments[static_cast<std::size_t>(selectedDefaultInstrumentIndex)];
        nameForLog = instrument.name;
        code = instrument.code;
        seedIndex = selectedDefaultInstrumentIndex + 1;
    }
    else if (selectedUserInstrumentIndex >= 0 && selectedUserInstrumentIndex < static_cast<int>(userInstruments.size()))
    {
        const auto& instrument = userInstruments[static_cast<std::size_t>(selectedUserInstrumentIndex)];
        nameForLog = instrument.name;
        code = instrument.code;
    }
    else
    {
        return;
    }

    int loaded = 0;

    for (const auto& synthDef : extractSynthDefs(code, 0, seedIndex))
    {
        if (embeddedScAudio.loadSynthDef(synthDef.name, synthDef.source))
            ++loaded;
        else
            statusLog.append("Instrument load failed: " + embeddedScAudio.getLastError());
    }

    applyChannelMappingsToEngine();

    if (loaded > 0)
        statusLog.append("Loaded SynthDef: " + nameForLog);

    userInstrumentCodeDirty = false;
    refreshInstrumentView();
    repaint();
}

void MainComponent::compileEditableDefaultSynthDefs()
{
    if (! embeddedScAudio.isReady())
        return;

    int loaded = 0;
    for (int index = 0; index < static_cast<int>(defaultInstruments.size()); ++index)
    {
        const auto& instrument = defaultInstruments[static_cast<std::size_t>(index)];
        if (instrument.code.isEmpty())
            continue;

        for (const auto& synthDef : extractSynthDefs(instrument.code, 0, index + 1))
            if (embeddedScAudio.loadSynthDef(synthDef.name, synthDef.source))
                ++loaded;
    }

    if (loaded > 0)
        statusLog.append("Loaded " + juce::String(loaded) + " editable default SynthDef" + (loaded == 1 ? "" : "s"));
}

void MainComponent::compileUserInstruments()
{
    if (! embeddedScAudio.isReady())
        return;

    int loaded = 0;
    int userIndex = 0;
    for (const auto& instrument : userInstruments)
    {
        ++userIndex;
        if (instrument.code.isEmpty())
            continue;

        for (const auto& synthDef : extractSynthDefs(instrument.code, 0, userIndex))
            if (embeddedScAudio.loadSynthDef(synthDef.name, synthDef.source))
                ++loaded;
    }

    applyChannelMappingsToEngine();

    if (loaded > 0)
        statusLog.append("Loaded " + juce::String(loaded) + " user instrument SynthDef" + (loaded == 1 ? "" : "s"));
}

void MainComponent::applyChannelInstrumentEditors()
{
    for (int channel = 0; channel < instrumentChannelCount; ++channel)
    {
        auto name = instrumentChannelSelectors[static_cast<std::size_t>(channel)].getText().trim();
        if (name.isEmpty())
            name = "tone";

        channelInstrumentMap[static_cast<std::size_t>(channel)] = name;
    }

    applyChannelMappingsToEngine();
    refreshInstrumentView();
    statusLog.append("Updated channel instrument map");
    repaint();
}

void MainComponent::applyChannelMappingsToEngine()
{
    for (int channel = 0; channel < instrumentChannelCount; ++channel)
        embeddedScAudio.setChannelInstrument(channel, channelInstrumentMap[static_cast<std::size_t>(channel)]);
}

void MainComponent::applyMixerControl(const int stateIndex, const int laneIndex, const int mixerControlIndex, const bool force)
{
    if (stateIndex < 0 || laneIndex < 0)
        return;

    if (mixerControlIndex < 0 || mixerControlIndex >= maximumMixerChannels - 1)
        return;

    {
        std::unique_lock lock(gridRuntimeMutex, std::defer_lock);

        if (force)
            lock.lock();
        else if (! lock.try_lock())
            return;

        if (stateIndex >= static_cast<int>(compositionStates.size()))
            return;

        auto& state = compositionStates[static_cast<std::size_t>(stateIndex)];

        if (laneIndex >= static_cast<int>(state.grids.size()))
            return;

        auto& lane = state.grids[static_cast<std::size_t>(laneIndex)];
        lane.mixerLevel = static_cast<float>(mixerLevelSliders[static_cast<std::size_t>(mixerControlIndex)].getValue());
        lane.mixerPan = static_cast<float>(mixerPanSliders[static_cast<std::size_t>(mixerControlIndex)].getValue());
    }
}

void MainComponent::applyMasterLevel()
{
    const auto masterIndex = juce::jlimit(0, maximumMixerChannels - 1, masterMixerControlIndex);
    masterLevel = static_cast<float>(mixerLevelSliders[static_cast<std::size_t>(masterIndex)].getValue());
    embeddedScAudio.setMasterLevel(masterLevel);
}

void MainComponent::applyLaneMixToEvents(std::vector<InternalEvent>& events, const CompositionGrid& lane, const float transitionGain) const
{
    const auto level = juce::jlimit(0.0f, 1.25f, lane.mixerLevel * transitionGain);

    for (auto& event : events)
    {
        std::visit([&lane, level](auto& typed)
        {
            typed.fields.velocity = juce::jlimit(0.0f, 1.0f, typed.fields.velocity * level);
            typed.fields.parameters["pan"] = juce::String(lane.mixerPan, 3);
        }, event);
    }
}

void MainComponent::configureLaneCodePane()
{
    addAndMakeVisible(laneScCodeEditor);
    laneCodeBackdrop.setVisible(false);
    laneScCodeEditor.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    laneScCodeEditor.setTabSize(4, true);
    laneScCodeEditor.setScrollbarThickness(8);
    laneScCodeDocumentListener.setCallback([this]
    {
        if (updatingLaneCodeEditor)
            return;

        const std::lock_guard lock(gridRuntimeMutex);

        if (compositionStates.empty())
            return;

        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

        if (state.grids.empty())
            return;

        activeGridSlot = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
        auto& lane = state.grids[static_cast<std::size_t>(activeGridSlot)];

        if (lane.kind == CompositionGrid::Kind::supercollider)
        {
            lane.scCode = laneScCodeDocument.getAllContent();
            lane.scCodeDirty = true;
            pendingLaneCodeCompile = true;
            lastLaneCodeEditMs = juce::Time::getMillisecondCounterHiRes();
        }
    });
    laneScCodeDocument.addListener(&laneScCodeDocumentListener);
    styleLaneCodePane();
    laneCodeBackdrop.setVisible(false);
    laneScCodeEditor.setVisible(false);
}

void MainComponent::styleLaneCodePane()
{
    laneScCodeEditor.setColour(juce::CodeEditorComponent::backgroundColourId, juce::Colour::fromRGB(24, 25, 26));
    laneScCodeEditor.setColour(juce::CodeEditorComponent::defaultTextColourId, lewittInk());
    laneScCodeEditor.setColour(juce::CodeEditorComponent::highlightColourId, lewittBlue().withAlpha(0.34f));
    laneScCodeEditor.setColour(juce::CodeEditorComponent::lineNumberBackgroundId, juce::Colour::fromRGB(34, 35, 36));
    laneScCodeEditor.setColour(juce::CodeEditorComponent::lineNumberTextId, lewittInk().withAlpha(0.42f));
    laneScCodeEditor.setColour(juce::ScrollBar::backgroundColourId, juce::Colour::fromRGB(35, 36, 37));
    laneScCodeEditor.setColour(juce::ScrollBar::thumbColourId, lewittBlue());
    laneScCodeEditor.setColourScheme(scCodeTokeniser.getDefaultColourScheme());
}

void MainComponent::styleGridSlotControls()
{
    const auto background = juce::Colour::fromRGB(48, 49, 50);
    const auto text = lewittInk();
    const auto outline = lewittLine().withAlpha(0.62f);

    for (auto* label : { &stateSlotLabel, &stateAdvanceLabel, &stateTimeSignatureLabel, &stateTimeSignatureSeparatorLabel,
                         &gridSlotLabel, &gridRatioLabel, &gridSizeLabel, &gridSizeSeparatorLabel })
    {
        label->setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));
        label->setJustificationType(juce::Justification::centredRight);
        label->setColour(juce::Label::textColourId, text);
    }

    stateSlotLabel.setJustificationType(juce::Justification::centred);
    stateAdvanceLabel.setJustificationType(juce::Justification::centred);
    gridSlotLabel.setJustificationType(juce::Justification::centred);
    gridRatioLabel.setJustificationType(juce::Justification::centred);

    for (auto* button : { &previousStateButton, &nextStateButton, &addStateButton,
                          &stateAdvanceModeButton, &previousGridButton, &nextGridButton, &addGridButton, &phaseModeButton, &laneKindButton })
    {
        button->setColour(juce::TextButton::buttonColourId, background);
        button->setColour(juce::TextButton::buttonOnColourId, lewittBlue().withAlpha(0.86f));
        button->setColour(juce::TextButton::textColourOffId, text);
        button->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        button->setColour(juce::ComboBox::outlineColourId, outline);
    }

    for (auto& button : gridTabButtons)
    {
        button.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(42, 43, 44));
        button.setColour(juce::TextButton::buttonOnColourId, lewittBlue().withAlpha(0.86f));
        button.setColour(juce::TextButton::textColourOffId, text);
        button.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        button.setColour(juce::ComboBox::outlineColourId, outline);
    }

    for (auto* editor : { &gridRatioEditor, &phaseOffsetEditor })
    {
        editor->setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
        editor->setJustification(juce::Justification::centred);
        editor->setColour(juce::TextEditor::backgroundColourId, background);
        editor->setColour(juce::TextEditor::outlineColourId, outline);
        editor->setColour(juce::TextEditor::focusedOutlineColourId, lewittBlue());
        editor->setColour(juce::TextEditor::highlightColourId, lewittBlue().withAlpha(0.35f));
        forceBlackEditorText(*editor);
    }

    for (auto* editor : { &stateAdvanceIntervalEditor, &stateTimeSignatureNumeratorEditor, &stateTimeSignatureDenominatorEditor })
    {
        editor->setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
        editor->setJustification(juce::Justification::centred);
        editor->setColour(juce::TextEditor::backgroundColourId, background);
        editor->setColour(juce::TextEditor::outlineColourId, outline);
        editor->setColour(juce::TextEditor::focusedOutlineColourId, lewittBlue());
        editor->setColour(juce::TextEditor::highlightColourId, lewittBlue().withAlpha(0.35f));
        forceBlackEditorText(*editor);
    }

    for (auto* editor : { &gridColumnsEditor, &gridRowsEditor })
    {
        editor->setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
        editor->setJustification(juce::Justification::centred);
        editor->setColour(juce::TextEditor::backgroundColourId, background);
        editor->setColour(juce::TextEditor::outlineColourId, outline);
        editor->setColour(juce::TextEditor::focusedOutlineColourId, lewittBlue());
        editor->setColour(juce::TextEditor::textColourId, text);
        editor->setColour(juce::TextEditor::highlightColourId, lewittBlue().withAlpha(0.35f));
    }
}

void MainComponent::updateGridSlotControls()
{
    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (compositionStates.empty())
        {
            stateSlotLabel.setText("S", juce::dontSendNotification);
            stateAdvanceLabel.setText("0/0", juce::dontSendNotification);
            gridSlotLabel.setText("L", juce::dontSendNotification);
            gridRatioLabel.setText("0/0", juce::dontSendNotification);
            previousStateButton.setEnabled(false);
            nextStateButton.setEnabled(false);
            addStateButton.setEnabled(true);
            stateAdvanceModeButton.setEnabled(false);
            stateAdvanceIntervalEditor.setEnabled(false);
            stateAdvanceModeButton.setButtonText("MAN");
            stateAdvanceIntervalEditor.setText("4", juce::dontSendNotification);
            stateTimeSignatureNumeratorEditor.setEnabled(false);
            stateTimeSignatureDenominatorEditor.setEnabled(false);
            stateTimeSignatureNumeratorEditor.setText("4", juce::dontSendNotification);
            stateTimeSignatureDenominatorEditor.setText("4", juce::dontSendNotification);
            forceBlackEditorText(stateAdvanceIntervalEditor);
            forceBlackEditorText(stateTimeSignatureNumeratorEditor);
            forceBlackEditorText(stateTimeSignatureDenominatorEditor);
            previousGridButton.setEnabled(false);
            nextGridButton.setEnabled(false);
            addGridButton.setEnabled(false);
            laneKindButton.setEnabled(false);
            gridRatioEditor.setText("1.0", juce::dontSendNotification);
            phaseOffsetEditor.setText("0", juce::dontSendNotification);
            gridColumnsEditor.setText(juce::String(GridModel::defaultWidth), juce::dontSendNotification);
            gridRowsEditor.setText(juce::String(GridModel::defaultHeight), juce::dontSendNotification);
            phaseModeButton.setButtonText("SYN");

            for (auto& button : gridTabButtons)
            {
                button.setVisible(false);
                button.setToggleState(false, juce::dontSendNotification);
            }

            return;
        }

        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

        if (state.grids.empty())
            state.grids.push_back({ makeEmptyGridSnapshot() });

        activeGridSlot = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
        const auto& grid = state.grids[static_cast<std::size_t>(activeGridSlot)];

        stateSlotLabel.setText("S", juce::dontSendNotification);
        stateAdvanceLabel.setText(juce::String(activeStateIndex + 1) + "/" + juce::String(static_cast<int>(compositionStates.size())),
                                  juce::dontSendNotification);
        const auto laneIsGrid = grid.kind == CompositionGrid::Kind::grid;
        gridSlotLabel.setText("L", juce::dontSendNotification);
        gridRatioLabel.setText(juce::String(activeGridSlot + 1) + "/" + juce::String(static_cast<int>(state.grids.size())),
                               juce::dontSendNotification);
        gridRatioEditor.setText(juce::String(grid.tempoRatio, 1), juce::dontSendNotification);
        phaseOffsetEditor.setText(juce::String(grid.phaseOffsetDegrees, 0), juce::dontSendNotification);
        forceBlackEditorText(gridRatioEditor);
        forceBlackEditorText(phaseOffsetEditor);
        gridColumnsEditor.setText(juce::String(grid.snapshot.width), juce::dontSendNotification);
        gridRowsEditor.setText(juce::String(grid.snapshot.height), juce::dontSendNotification);
        laneKindButton.setButtonText(laneIsGrid ? "G" : "SC");
        stateAdvanceModeButton.setButtonText(compactStateAdvanceModeText(getStateAdvanceModeText(state.advanceMode)));
        stateAdvanceIntervalEditor.setText(juce::String(state.advanceInterval), juce::dontSendNotification);
        stateTimeSignatureNumeratorEditor.setText(juce::String(state.timeSignatureNumerator), juce::dontSendNotification);
        stateTimeSignatureDenominatorEditor.setText(juce::String(state.timeSignatureDenominator), juce::dontSendNotification);
        forceBlackEditorText(stateAdvanceIntervalEditor);
        forceBlackEditorText(stateTimeSignatureNumeratorEditor);
        forceBlackEditorText(stateTimeSignatureDenominatorEditor);
        phaseModeButton.setButtonText(grid.phaseOffsetEnabled ? "OFF" : "SYN");
        phaseModeButton.setToggleState(grid.phaseOffsetEnabled, juce::dontSendNotification);

        const auto canSwitchState = compositionStates.size() > 1;
        const auto canSwitch = state.grids.size() > 1;
        previousStateButton.setEnabled(canSwitchState);
        nextStateButton.setEnabled(canSwitchState);
        addStateButton.setEnabled(compositionStates.size() < maximumCompositionStates);
        stateAdvanceModeButton.setEnabled(true);
        stateAdvanceIntervalEditor.setEnabled(state.advanceMode != CompositionState::AdvanceMode::manual
                                              && state.advanceMode != CompositionState::AdvanceMode::trigger);
        stateTimeSignatureNumeratorEditor.setEnabled(true);
        stateTimeSignatureDenominatorEditor.setEnabled(true);
        laneKindButton.setEnabled(true);
        gridRatioEditor.setEnabled(laneIsGrid);
        phaseModeButton.setEnabled(laneIsGrid);
        phaseOffsetEditor.setEnabled(laneIsGrid && grid.phaseOffsetEnabled);
        gridColumnsEditor.setEnabled(laneIsGrid);
        gridRowsEditor.setEnabled(laneIsGrid);
        previousGridButton.setEnabled(canSwitch);
        nextGridButton.setEnabled(canSwitch);
        addGridButton.setEnabled(state.grids.size() < maximumGridsPerState);

        for (int index = 0; index < static_cast<int>(gridTabButtons.size()); ++index)
        {
            auto& button = gridTabButtons[static_cast<std::size_t>(index)];
            const auto tabVisible = index < static_cast<int>(state.grids.size());
            button.setVisible(tabVisible);
            button.setEnabled(tabVisible);
            button.setToggleState(tabVisible && index == activeGridSlot, juce::dontSendNotification);

            if (tabVisible)
            {
                const auto& tabGrid = state.grids[static_cast<std::size_t>(index)];
                const auto kind = tabGrid.kind == CompositionGrid::Kind::grid ? "G" : "SC";
                button.setButtonText(juce::String(index + 1).paddedLeft('0', 2) + " " + kind);
            }
        }
    }

    showActiveLane();
    refreshStateGraph();
    refreshMixerView();
    refreshArrangementView();
    resized();
}

void MainComponent::toggleSelectedLaneKind()
{
    storeActiveGridSlot();
    bool isGrid = true;

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (compositionStates.empty())
            return;

        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

        if (state.grids.empty())
            state.grids.push_back({ makeEmptyGridSnapshot() });

        activeGridSlot = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
        auto& lane = state.grids[static_cast<std::size_t>(activeGridSlot)];

        if (lane.kind == CompositionGrid::Kind::grid)
        {
            lane.kind = CompositionGrid::Kind::supercollider;

            if (lane.scCode.isEmpty())
                lane.scCode = createDefaultScLaneCode(activeStateIndex + 1, activeGridSlot + 1);

            lane.scCodeDirty = true;
            isGrid = false;
        }
        else
        {
            lane.kind = CompositionGrid::Kind::grid;
            isGrid = true;
        }
    }

    updateGridSlotControls();
    showActiveLane();
    if (! isGrid)
        compileSelectedScLane();
    statusLog.append(isGrid ? "Lane set to grid" : "Lane set to SuperCollider");
    repaint();
}

void MainComponent::applyGridSizeEditors()
{
    const auto columns = juce::jlimit(1, maximumGridColumns, gridColumnsEditor.getText().getIntValue());
    const auto rows = juce::jlimit(1, maximumGridRows, gridRowsEditor.getText().getIntValue());
    bool changed = false;

    storeActiveGridSlot();

    {
        const std::lock_guard lock(gridRuntimeMutex);
        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

        if (state.grids.empty())
            state.grids.push_back({ makeEmptyGridSnapshot() });

        activeGridSlot = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
        auto& grid = state.grids[static_cast<std::size_t>(activeGridSlot)];

        changed = grid.snapshot.width != columns || grid.snapshot.height != rows;
        grid.snapshot = resizeSnapshot(std::move(grid.snapshot), columns, rows);
        grid.lastEvaluatedFrame = std::numeric_limits<std::uint64_t>::max();
        gridModel.applySnapshot(grid.snapshot);
    }

    updateGridSizeControls();
    gridEditor.clearPlayhead();
    gridEditor.fitToView();
    gridEditor.repaint();

    if (changed)
        statusLog.append("Grid size set to " + juce::String(columns) + "x" + juce::String(rows));

    repaint();
}

void MainComponent::updateGridSizeControls()
{
    gridColumnsEditor.setText(juce::String(juce::jlimit(1, maximumGridColumns, gridModel.getWidth())),
                              juce::dontSendNotification);
    gridRowsEditor.setText(juce::String(juce::jlimit(1, maximumGridRows, gridModel.getHeight())),
                           juce::dontSendNotification);
}

void MainComponent::storeActiveGridSlot()
{
    const std::lock_guard lock(gridRuntimeMutex);
    storeActiveGridSlotLocked();
}

void MainComponent::storeActiveLane()
{
    storeActiveGridSlot();
}

void MainComponent::storeActiveLaneLocked()
{
    storeActiveGridSlotLocked();
}

void MainComponent::storeActiveGridSlotLocked()
{
    if (compositionStates.empty())
    {
        CompositionState state;
        state.name = "State 01";
        state.transitionCode = createDefaultTransitionCode(1);
        state.grids.push_back({ gridModel.createSnapshot() });
        compositionStates.push_back(std::move(state));
        activeStateIndex = 0;
        activeGridSlot = 0;
        return;
    }

    activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
    auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

    if (state.grids.empty())
        state.grids.push_back({ makeEmptyGridSnapshot() });

    activeGridSlot = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
    auto& lane = state.grids[static_cast<std::size_t>(activeGridSlot)];

    if (lane.kind == CompositionGrid::Kind::grid)
        lane.snapshot = gridModel.createSnapshot();
    else if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        lane.scCode = laneScCodeDocument.getAllContent();
        lane.scCodeDirty = true;
    }
}

GridModel::Snapshot MainComponent::makeEmptyGridSnapshot(const int columns, const int rows) const
{
    GridModel::Snapshot snapshot;
    snapshot.width = juce::jlimit(1, maximumGridColumns, columns);
    snapshot.height = juce::jlimit(1, maximumGridRows, rows);
    snapshot.cells.assign(static_cast<std::size_t>(snapshot.width * snapshot.height), GridModel::emptyGlyph);
    return snapshot;
}

void MainComponent::showActiveLane()
{
    bool showGrid = true;
    bool shouldCompile = false;
    juce::String code;

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (! compositionStates.empty())
        {
            activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
            auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

            if (state.grids.empty())
                state.grids.push_back({ makeEmptyGridSnapshot() });

            activeGridSlot = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
            auto& lane = state.grids[static_cast<std::size_t>(activeGridSlot)];
            showGrid = lane.kind == CompositionGrid::Kind::grid;

            if (showGrid)
                gridModel.applySnapshot(lane.snapshot);
            else
            {
                if (lane.scCode.isEmpty())
                    lane.scCode = createDefaultScLaneCode(activeStateIndex + 1, activeGridSlot + 1);

                code = lane.scCode;
                shouldCompile = lane.scCodeDirty;
            }
        }
    }

    gridEditor.setVisible(showGrid);
    laneCodeBackdrop.setVisible(false);
    laneScCodeEditor.setVisible(! showGrid);
    laneKindButton.setButtonText(showGrid ? "G" : "SC");

    if (showGrid)
    {
        gridEditor.fitToView();
        gridEditor.repaint();
    }
    else
    {
        updatingLaneCodeEditor = true;
        laneScCodeDocument.replaceAllContent(code);
        updatingLaneCodeEditor = false;
        laneScCodeEditor.setHighlightedRegion(juce::Range<int>());

        if (shouldCompile && ! transportEngine.isPlaying())
            compileSelectedScLane();
    }
}

juce::String MainComponent::createDefaultScLaneCode(const int stateNumber, const int laneNumber) const
{
    const auto name = "gc_s" + juce::String(stateNumber) + "_l" + juce::String(laneNumber);

    return "SynthDef(\\"
        + name
        + R"SC(, { |out = 0, pitch = 60, amp = 0.25, sustain = 0.35, pan = 0|
    var freq = pitch.midicps;
    var env = EnvGen.kr(Env.perc(0.004, sustain), doneAction: 2);
    var sig = SinOsc.ar(freq) * env * amp;
    Out.ar(out, Pan2.ar(sig, pan));
});
)SC";
}

std::vector<juce::String> MainComponent::getSynthDefNamesFromSource(const juce::String& source,
                                                                    const int stateNumber,
                                                                    const int laneNumber) const
{
    std::vector<juce::String> names;

    for (const auto& synthDef : extractSynthDefs(source, stateNumber, laneNumber))
        names.push_back(synthDef.name);

    return names;
}

juce::String MainComponent::getSynthDefNameFromSource(const juce::String& source, const int stateNumber, const int laneNumber) const
{
    return getSynthDefNamesFromSource(source, stateNumber, laneNumber).front();
}

void MainComponent::compileSelectedScLane()
{
    if (! embeddedScAudio.isReady())
    {
        statusLog.append("SC lane compile skipped: audio is not ready");
        return;
    }

    std::vector<ExtractedSynthDef> synthDefs;

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (compositionStates.empty())
            return;

        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

        if (state.grids.empty())
            return;

        activeGridSlot = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
        auto& lane = state.grids[static_cast<std::size_t>(activeGridSlot)];

        if (lane.kind != CompositionGrid::Kind::supercollider)
            return;

        lane.scCode = laneScCodeDocument.getAllContent();
        synthDefs = extractSynthDefs(lane.scCode, activeStateIndex + 1, activeGridSlot + 1);
        lane.scSynthName = synthDefs.front().name;
        lane.scCodeDirty = false;
    }

    int loaded = 0;

    for (const auto& synthDef : synthDefs)
    {
        if (embeddedScAudio.loadSynthDef(synthDef.name, synthDef.source))
            ++loaded;
        else
            statusLog.append("SynthDef load failed: " + embeddedScAudio.getLastError());
    }

    if (loaded > 0)
        statusLog.append("Loaded " + juce::String(loaded) + " SynthDef" + (loaded == 1 ? "" : "s"));

    repaint();
}

void MainComponent::compileScLanesForState(const int stateIndex)
{
    if (! embeddedScAudio.isReady())
    {
        return;
    }

    struct PendingSynth
    {
        juce::String name;
        juce::String code;
    };

    std::vector<PendingSynth> pending;

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (stateIndex < 0 || stateIndex >= static_cast<int>(compositionStates.size()))
            return;

        auto& state = compositionStates[static_cast<std::size_t>(stateIndex)];

        for (int laneIndex = 0; laneIndex < static_cast<int>(state.grids.size()); ++laneIndex)
        {
            auto& lane = state.grids[static_cast<std::size_t>(laneIndex)];

            if (lane.kind != CompositionGrid::Kind::supercollider)
                continue;

            if (lane.scCode.isEmpty())
                lane.scCode = createDefaultScLaneCode(stateIndex + 1, laneIndex + 1);

            const auto synthDefs = extractSynthDefs(lane.scCode, stateIndex + 1, laneIndex + 1);
            lane.scSynthName = synthDefs.front().name;
            lane.scCodeDirty = false;

            for (const auto& synthDef : synthDefs)
                pending.push_back({ synthDef.name, synthDef.source });
        }
    }

    for (const auto& synth : pending)
    {
        if (! embeddedScAudio.loadSynthDef(synth.name, synth.code))
            statusLog.append("SynthDef load failed: " + embeddedScAudio.getLastError());
    }
}

void MainComponent::compileScLanesForAllStates()
{
    if (! embeddedScAudio.isReady())
    {
        return;
    }

    struct PendingSynth
    {
        juce::String name;
        juce::String code;
    };

    std::vector<PendingSynth> pending;

    {
        const std::lock_guard lock(gridRuntimeMutex);
        juce::StringArray seen;

        for (int stateIndex = 0; stateIndex < static_cast<int>(compositionStates.size()); ++stateIndex)
        {
            auto& state = compositionStates[static_cast<std::size_t>(stateIndex)];

            for (int laneIndex = 0; laneIndex < static_cast<int>(state.grids.size()); ++laneIndex)
            {
                auto& lane = state.grids[static_cast<std::size_t>(laneIndex)];

                if (lane.kind != CompositionGrid::Kind::supercollider)
                    continue;

                if (lane.scCode.isEmpty())
                    lane.scCode = createDefaultScLaneCode(stateIndex + 1, laneIndex + 1);

                const auto synthDefs = extractSynthDefs(lane.scCode, stateIndex + 1, laneIndex + 1);
                lane.scSynthName = synthDefs.front().name;
                lane.scCodeDirty = false;

                for (const auto& synthDef : synthDefs)
                {
                    const auto key = synthDef.name + ":" + juce::String(static_cast<juce::int64>(synthDef.source.hashCode64()));

                    if (! seen.contains(key))
                    {
                        seen.add(key);
                        pending.push_back({ synthDef.name, synthDef.source });
                    }
                }
            }
        }
    }

    int loaded = 0;
    int failed = 0;

    for (const auto& synth : pending)
    {
        if (embeddedScAudio.loadSynthDef(synth.name, synth.code))
            ++loaded;
        else
        {
            ++failed;
        }
    }

    if (loaded > 0)
        statusLog.append("Loaded " + juce::String(loaded) + " SC SynthDef" + (loaded == 1 ? "" : "s"));

    if (failed > 0)
        statusLog.append("SynthDef load failed: " + embeddedScAudio.getLastError());
}

void MainComponent::switchToState(const int stateIndex)
{
    if (stateIndex < 0 || stateIndex >= static_cast<int>(compositionStates.size()))
    {
        statusLog.append("No state " + juce::String(stateIndex + 1));
        repaint();
        return;
    }

    const auto wasPlaying = transportEngine.isPlaying();

    if (wasPlaying)
        transportEngine.pause();

    storeActiveGridSlot();
    storeActiveTransitionCode();

    double stateBpm = 120.0;
    int gridCount = 1;

    {
        const std::lock_guard lock(gridRuntimeMutex);
        activeStateIndex = stateIndex;
        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

        if (state.grids.empty())
            state.grids.push_back({ makeEmptyGridSnapshot() });

        activeGridSlot = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
        gridCount = static_cast<int>(state.grids.size());
        stateBpm = state.bpm;
        gridModel.applySnapshot(state.grids[static_cast<std::size_t>(activeGridSlot)].snapshot);
    }

    transportEngine.setBpm(stateBpm);
    transportEngine.reset();
    lastTransportFrame = 0;
    lastTickInBeat = 0;
    gridEditor.clearPlayhead();
    gridEditor.fitToView();
    gridEditor.repaint();
    resetGridRuntimeClocks();

    if (wasPlaying)
        transportEngine.start();

    updateTransportControls();
    updateGridSlotControls();
    updateStateAdvanceControls();
    showActiveTransitionCode();
    compileScLanesForState(activeStateIndex);
    refreshArrangementView();
    statusLog.append("Selected state " + juce::String(activeStateIndex + 1) + " with " + juce::String(gridCount) + " grids");
    repaint();
}

void MainComponent::previousState()
{
    if (compositionStates.size() <= 1)
    {
        statusLog.append("Only one state");
        repaint();
        return;
    }

    switchToState((activeStateIndex + static_cast<int>(compositionStates.size()) - 1) % static_cast<int>(compositionStates.size()));
}

void MainComponent::nextState()
{
    if (compositionStates.size() <= 1)
    {
        statusLog.append("Only one state");
        repaint();
        return;
    }

    switchToState((activeStateIndex + 1) % static_cast<int>(compositionStates.size()));
}

void MainComponent::addCompositionState()
{
    const auto wasPlaying = transportEngine.isPlaying();

    if (wasPlaying)
        transportEngine.pause();

    storeActiveGridSlot();

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (compositionStates.size() >= maximumCompositionStates)
        {
            statusLog.append("Composition already has 16 states");

            if (wasPlaying)
                transportEngine.start();

            repaint();
            return;
        }

        CompositionGrid grid;
        grid.snapshot = makeEmptyGridSnapshot();

        CompositionState state;
        state.name = "State " + juce::String(static_cast<int>(compositionStates.size()) + 1).paddedLeft('0', 2);
        state.bpm = transportEngine.getBpm();
        state.transitionCode = createDefaultTransitionCode(static_cast<int>(compositionStates.size()) + 1);
        state.grids.push_back(std::move(grid));

        compositionStates.push_back(std::move(state));
        activeStateIndex = static_cast<int>(compositionStates.size()) - 1;
        activeGridSlot = 0;
        gridModel.applySnapshot(compositionStates.back().grids.front().snapshot);
    }

    transportEngine.reset();
    lastTransportFrame = 0;
    lastTickInBeat = 0;
    activeStateEntryFrame = 0;
    gridEditor.clearPlayhead();
    gridEditor.fitToView();
    gridEditor.repaint();
    resetGridRuntimeClocks();

    if (wasPlaying)
        transportEngine.start();

    updateGridSlotControls();
    updateStateAdvanceControls();
    showActiveTransitionCode();
    refreshArrangementView();
    statusLog.append("Added state " + juce::String(activeStateIndex + 1));
    repaint();
}

void MainComponent::copySelectedState()
{
    storeActiveGridSlot();
    storeActiveTransitionCode();

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (compositionStates.empty())
            return;

        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        copiedState = compositionStates[static_cast<std::size_t>(activeStateIndex)];
    }

    statusLog.append("Copied state " + juce::String(activeStateIndex + 1));
    repaint();
}

void MainComponent::pasteCopiedState()
{
    if (! copiedState.has_value())
    {
        statusLog.append("No copied state");
        repaint();
        return;
    }

    const auto wasPlaying = transportEngine.isPlaying();

    if (wasPlaying)
        transportEngine.pause();

    storeActiveGridSlot();
    storeActiveTransitionCode();

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (compositionStates.size() >= maximumCompositionStates)
        {
            statusLog.append("Composition already has 16 states");

            if (wasPlaying)
                transportEngine.start();

            repaint();
            return;
        }

        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        auto pasted = *copiedState;
        pasted.name = "State " + juce::String(static_cast<int>(compositionStates.size()) + 1).paddedLeft('0', 2);

        if (pasted.grids.empty())
            pasted.grids.push_back({ makeEmptyGridSnapshot() });

        const auto insertIndex = activeStateIndex + 1;
        compositionStates.insert(compositionStates.begin() + insertIndex, std::move(pasted));
        activeStateIndex = insertIndex;
        activeGridSlot = 0;
        activeStateEntryFrame = lastTransportFrame;
        gridModel.applySnapshot(compositionStates[static_cast<std::size_t>(activeStateIndex)].grids.front().snapshot);
    }

    resetGridRuntimeClocks();
    gridEditor.clearPlayhead();
    gridEditor.fitToView();

    if (wasPlaying)
        transportEngine.start();

    updateTransportControls();
    updateGridSlotControls();
    updateStateAdvanceControls();
    showActiveTransitionCode();
    refreshStateGraph();
    refreshArrangementView();
    statusLog.append("Pasted state " + juce::String(activeStateIndex + 1));
    repaint();
}

void MainComponent::deleteSelectedState()
{
    if (compositionStates.size() <= 1)
    {
        statusLog.append("Cannot delete the only state");
        repaint();
        return;
    }

    const auto wasPlaying = transportEngine.isPlaying();

    if (wasPlaying)
        transportEngine.pause();

    storeActiveGridSlot();
    storeActiveTransitionCode();

    int deletedState = activeStateIndex + 1;
    double stateBpm = transportEngine.getBpm();

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (compositionStates.size() <= 1)
            return;

        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        deletedState = activeStateIndex + 1;
        compositionStates.erase(compositionStates.begin() + activeStateIndex);
        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);

        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

        if (state.grids.empty())
            state.grids.push_back({ makeEmptyGridSnapshot() });

        activeGridSlot = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
        activeStateEntryFrame = lastTransportFrame;
        stateBpm = state.bpm;
        gridModel.applySnapshot(state.grids[static_cast<std::size_t>(activeGridSlot)].snapshot);
    }

    transportEngine.setBpm(stateBpm);
    resetGridRuntimeClocks();
    gridEditor.clearPlayhead();
    gridEditor.fitToView();

    if (wasPlaying)
        transportEngine.start();

    updateTransportControls();
    updateGridSlotControls();
    updateStateAdvanceControls();
    showActiveTransitionCode();
    refreshStateGraph();
    refreshArrangementView();
    statusLog.append("Deleted state " + juce::String(deletedState));
    repaint();
}

void MainComponent::switchToGridSlot(const int slotIndex)
{
    const auto wasPlaying = transportEngine.isPlaying();

    if (wasPlaying)
        transportEngine.pause();

    storeActiveGridSlot();

    int gridCount = 0;

    {
        const std::lock_guard lock(gridRuntimeMutex);
        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];
        gridCount = static_cast<int>(state.grids.size());

        if (slotIndex < 0 || slotIndex >= gridCount)
        {
            statusLog.append("No grid " + juce::String(slotIndex + 1) + " in this state");

            if (wasPlaying)
                transportEngine.start();

            repaint();
            return;
        }

        activeGridSlot = slotIndex;
    }

    transportEngine.reset();
    lastTransportFrame = 0;
    lastTickInBeat = 0;
    gridEditor.clearPlayhead();
    gridEditor.fitToView();
    gridEditor.repaint();
    resetGridRuntimeClocks();

    if (wasPlaying)
        transportEngine.start();

    updateGridSlotControls();
    showActiveLane();
    statusLog.append("Selected lane " + juce::String(activeGridSlot + 1) + "/" + juce::String(gridCount));
    repaint();
}

void MainComponent::previousGridSlot()
{
    const auto gridCount = compositionStates.empty() ? 0 : static_cast<int>(compositionStates[static_cast<std::size_t>(activeStateIndex)].grids.size());

    if (gridCount <= 1)
    {
        statusLog.append("Only one grid in this state");
        repaint();
        return;
    }

    switchToGridSlot((activeGridSlot + gridCount - 1) % gridCount);
}

void MainComponent::nextGridSlot()
{
    const auto gridCount = compositionStates.empty() ? 0 : static_cast<int>(compositionStates[static_cast<std::size_t>(activeStateIndex)].grids.size());

    if (gridCount <= 1)
    {
        statusLog.append("Only one grid in this state");
        repaint();
        return;
    }

    switchToGridSlot((activeGridSlot + 1) % gridCount);
}

void MainComponent::addGridSlot()
{
    const auto wasPlaying = transportEngine.isPlaying();

    if (wasPlaying)
        transportEngine.pause();

    storeActiveGridSlot();

    {
        const std::lock_guard lock(gridRuntimeMutex);
        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

        if (state.grids.size() >= maximumGridsPerState)
        {
            statusLog.append("State already has 8 grids");

            if (wasPlaying)
                transportEngine.start();

            repaint();
            return;
        }

        CompositionGrid grid;
        grid.snapshot = makeEmptyGridSnapshot();
        state.grids.push_back(std::move(grid));
        activeGridSlot = static_cast<int>(state.grids.size()) - 1;
        gridModel.applySnapshot(state.grids.back().snapshot);
    }

    transportEngine.reset();
    lastTransportFrame = 0;
    lastTickInBeat = 0;
    gridEditor.clearPlayhead();
    gridEditor.fitToView();
    gridEditor.repaint();
    resetGridRuntimeClocks();

    if (wasPlaying)
        transportEngine.start();

    updateGridSlotControls();
    statusLog.append("Added grid " + juce::String(activeGridSlot + 1));
    repaint();
}

void MainComponent::applyGridTimingEditors()
{
    const auto requestedRatio = gridRatioEditor.getText().getDoubleValue();
    const auto ratio = juce::jlimit(minimumGridTempoRatio,
                                    maximumGridTempoRatio,
                                    requestedRatio > 0.0 ? requestedRatio : 1.0);
    const auto requestedPhase = phaseOffsetEditor.getText().getDoubleValue();
    const auto phaseOffset = juce::jlimit(0.0, 360.0, requestedPhase);

    {
        const std::lock_guard lock(gridRuntimeMutex);
        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

        if (state.grids.empty())
            state.grids.push_back({ makeEmptyGridSnapshot() });

        activeGridSlot = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
        auto& grid = state.grids[static_cast<std::size_t>(activeGridSlot)];
        grid.tempoRatio = ratio;
        grid.phaseOffsetDegrees = phaseOffset;
        grid.lastEvaluatedFrame = std::numeric_limits<std::uint64_t>::max();
    }

    updateGridSlotControls();
    statusLog.append("Lane timing set to 1:" + juce::String(ratio, 1) + " phase " + juce::String(phaseOffset, 0));
    repaint();
}

void MainComponent::toggleSelectedGridPhaseMode()
{
    bool offsetEnabled = false;

    {
        const std::lock_guard lock(gridRuntimeMutex);
        activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

        if (state.grids.empty())
            state.grids.push_back({ makeEmptyGridSnapshot() });

        activeGridSlot = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
        auto& grid = state.grids[static_cast<std::size_t>(activeGridSlot)];
        grid.phaseOffsetEnabled = ! grid.phaseOffsetEnabled;
        grid.lastEvaluatedFrame = std::numeric_limits<std::uint64_t>::max();
        offsetEnabled = grid.phaseOffsetEnabled;
    }

    updateGridSlotControls();
    statusLog.append(offsetEnabled ? "Lane phase offset enabled" : "Lane phase synced");
    repaint();
}

double MainComponent::getPhaseOffsetFrameDelta(const double ratio, const double degrees) const
{
    const auto clampedRatio = juce::jlimit(minimumGridTempoRatio, maximumGridTempoRatio, ratio);
    const auto clampedDegrees = juce::jlimit(0.0, 360.0, degrees);
    return clampedRatio * (clampedDegrees / 360.0);
}

void MainComponent::resetGridRuntimeClocks()
{
    const std::lock_guard lock(gridRuntimeMutex);

    for (auto& state : compositionStates)
        for (auto& grid : state.grids)
            grid.lastEvaluatedFrame = std::numeric_limits<std::uint64_t>::max();
}

std::uint64_t MainComponent::getDisplayGridFrame(const std::uint64_t stateFrame) const
{
    const std::lock_guard lock(gridRuntimeMutex);

    if (compositionStates.empty())
        return stateFrame;

    const auto localStateFrame = stateFrame >= activeStateEntryFrame ? stateFrame - activeStateEntryFrame : 0;

    const auto stateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
    const auto& state = compositionStates[static_cast<std::size_t>(stateIndex)];

    if (state.grids.empty())
        return stateFrame;

    const auto gridIndex = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
    const auto& grid = state.grids[static_cast<std::size_t>(gridIndex)];
    const auto ratio = juce::jlimit(minimumGridTempoRatio, maximumGridTempoRatio, grid.tempoRatio);
    const auto phaseOffset = grid.phaseOffsetEnabled ? getPhaseOffsetFrameDelta(ratio, grid.phaseOffsetDegrees) : 0.0;
    return static_cast<std::uint64_t>(std::floor((static_cast<double>(localStateFrame) + phaseOffset) / ratio));
}

GridEvaluation MainComponent::evaluateActiveState(const TransportEngine::TickContext& context)
{
    if (context.frame != 0)
    {
        CompositionState::AdvanceMode mode = CompositionState::AdvanceMode::manual;
        int interval = 1;
        int currentState = 1;
        std::uint64_t stateEntryFrame = 0;
        int timeSignatureNumerator = 4;
        int timeSignatureDenominator = 4;
        juce::String transitionCode;

        {
            const std::lock_guard lock(gridRuntimeMutex);

            if (! compositionStates.empty())
            {
                const auto stateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
                const auto& state = compositionStates[static_cast<std::size_t>(stateIndex)];
                currentState = stateIndex + 1;
                stateEntryFrame = activeStateEntryFrame;
                mode = state.advanceMode;
                interval = juce::jlimit(1, 999, state.advanceInterval);
                timeSignatureNumerator = juce::jlimit(1, 32, state.timeSignatureNumerator);
                timeSignatureDenominator = juce::jlimit(1, 32, state.timeSignatureDenominator);
                transitionCode = state.transitionCode;
            }
        }

        bool shouldAdvance = false;
        TransitionContext transitionContext;
        transitionContext.frame = context.frame;
        transitionContext.state = currentState;
        transitionContext.stateFrame = context.frame >= stateEntryFrame ? context.frame - stateEntryFrame : 0;
        transitionContext.beat = static_cast<int>(transitionContext.stateFrame / static_cast<std::uint64_t>(juce::jmax(1, context.ticksPerBeat)));
        {
            const auto quarterBeatsPerBar = static_cast<double>(timeSignatureNumerator) * 4.0
                                            / static_cast<double>(juce::jmax(1, timeSignatureDenominator));
            const auto framesPerBar = static_cast<double>(juce::jmax(1, context.ticksPerBeat)) * quarterBeatsPerBar;
            transitionContext.bar = framesPerBar > 0.0
                                        ? static_cast<int>(std::floor(static_cast<double>(transitionContext.stateFrame) / framesPerBar))
                                        : 0;
        }

        if (context.isBeat && mode == CompositionState::AdvanceMode::beats)
        {
            shouldAdvance = transitionContext.beat >= interval;
        }
        else if (context.isBeat && mode == CompositionState::AdvanceMode::bars)
        {
            shouldAdvance = transitionContext.bar >= interval;
        }

        if (shouldAdvance)
        {
            const auto rules = parseTransitionRules(transitionCode);
            if (! rules.linear.empty() || ! rules.weighted.empty() || ! rules.cycle.empty() || ! rules.triggers.empty())
            {
                const auto targetState = chooseTransitionTarget(rules, currentState, transitionContext);

                if (targetState > 0 && targetState != currentState)
                {
                    double stateBpm = context.bpm;

                    if (applyTransitionTargetForTransport(targetState, context.frame, stateBpm))
                        transportEngine.setBpm(stateBpm);
                }
                else if (targetState == currentState)
                {
                    const std::lock_guard lock(gridRuntimeMutex);
                    activeStateEntryFrame = context.frame;
                }
            }
        }
    }

    const std::lock_guard lock(gridRuntimeMutex);

    GridEvaluation combined;

    if (compositionStates.empty())
        return combined;

    activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
    auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];
    const auto stateFrame = context.frame >= activeStateEntryFrame ? context.frame - activeStateEntryFrame : 0;

    if (state.grids.empty())
        state.grids.push_back({ makeEmptyGridSnapshot() });

    activeGridSlot = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
    const auto transitionGain = [&context, stateFrame]
    {
        const auto fadeFrames = static_cast<std::uint64_t>(juce::jmax(1, context.ticksPerBeat) * 4);

        if (stateFrame >= fadeFrames)
            return 1.0f;

        const auto firstBeatPosition = static_cast<float>(stateFrame)
                                       / static_cast<float>(fadeFrames);
        const auto eased = firstBeatPosition * firstBeatPosition * (3.0f - 2.0f * firstBeatPosition);
        return juce::jlimit(0.08f, 1.0f, 0.08f + eased * 0.92f);
    }();

    for (int index = 0; index < static_cast<int>(state.grids.size()); ++index)
    {
        auto& grid = state.grids[static_cast<std::size_t>(index)];

        if (grid.kind != CompositionGrid::Kind::grid)
            continue;

        const auto ratio = juce::jlimit(minimumGridTempoRatio, maximumGridTempoRatio, grid.tempoRatio);
        const auto phaseOffset = grid.phaseOffsetEnabled ? getPhaseOffsetFrameDelta(ratio, grid.phaseOffsetDegrees) : 0.0;
        const auto gridFrame = static_cast<std::uint64_t>(std::floor((static_cast<double>(stateFrame) + phaseOffset) / ratio));

        if (gridFrame == grid.lastEvaluatedFrame)
            continue;

        grid.lastEvaluatedFrame = gridFrame;
        auto evaluation = gridInterpreter.evaluate(grid.snapshot, gridFrame);

        if (evaluation.grid.width > 0 && evaluation.grid.height > 0)
            grid.snapshot = evaluation.grid;

        applyLaneMixToEvents(evaluation.events, grid, transitionGain);

        float lanePeak = 0.0f;
        for (const auto& event : evaluation.events)
        {
            std::visit([&lanePeak](const auto& typed)
            {
                lanePeak = juce::jmax(lanePeak, typed.fields.velocity);
            }, event);
        }

        if (lanePeak > 0.0f)
        {
            const auto meterIndex = activeStateIndex * maximumGridsPerState + index;
            if (meterIndex >= 0 && meterIndex < maximumMixerChannels - 1)
            {
                auto& meter = mixerMeterPeaks[static_cast<std::size_t>(meterIndex)];
                const auto current = meter.load(std::memory_order_relaxed);
                if (lanePeak > current)
                    meter.store(juce::jlimit(0.0f, 1.0f, lanePeak), std::memory_order_relaxed);
            }
        }

        combined.events.insert(combined.events.end(), evaluation.events.begin(), evaluation.events.end());

        if (index == activeGridSlot)
        {
            combined.grid = grid.snapshot;
            gridModel.applySnapshot(grid.snapshot);
        }
    }

    if (combined.grid.width <= 0 || combined.grid.height <= 0)
        combined.grid = state.grids[static_cast<std::size_t>(activeGridSlot)].snapshot;

    embeddedScAudio.setTransport(context.bpm, context.frame, true);
    embeddedScAudio.enqueue(combined.events);

    return combined;
}

void MainComponent::toggleTransportPlayback()
{
    applyTransportEditors();
    storeActiveGridSlot();

    if (transportEngine.isPlaying())
    {
        transportEngine.pause();
        statusLog.append("Transport paused");
    }
    else
    {
        transportEngine.start();
        statusLog.append("Transport running");
    }

    updateTransportControls();
    refreshStateGraph();
    refreshArrangementView();
    repaint();
}

void MainComponent::resetTransport()
{
    transportEngine.reset();
    lastTransportFrame = 0;
    lastTickInBeat = 0;
    activeStateEntryFrame = 0;
    gridEditor.clearPlayhead();
    resetGridRuntimeClocks();
    statusLog.append("Transport reset");
    updateTransportControls();
    refreshStateGraph();
    refreshArrangementView();
    repaint();
}

void MainComponent::applyTransportEditors()
{
    const auto bpm = bpmEditor.getText().getDoubleValue();
    transportEngine.setBpm(bpm > 0.0 ? bpm : 120.0);
    bpmEditor.setText(juce::String(transportEngine.getBpm(), 1), juce::dontSendNotification);
    forceBlackEditorText(bpmEditor);

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (! compositionStates.empty())
        {
            activeStateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
            compositionStates[static_cast<std::size_t>(activeStateIndex)].bpm = transportEngine.getBpm();
        }
    }

    refreshStateGraph();
    refreshArrangementView();
}

MainComponent::TransitionRules MainComponent::parseTransitionRules(juce::String text) const
{
    TransitionRules rules;
    juce::StringArray lines;
    lines.addLines(text);
    juce::String uncommented;

    for (auto line : lines)
    {
        const auto commentStart = line.indexOf("//");

        if (commentStart >= 0)
            line = line.substring(0, commentStart);

        uncommented += line + "\n";
    }

    const auto source = uncommented.toStdString();
    std::smatch blockMatch;
    const auto parseChoiceBlock = [](const std::string& choicesBlock)
    {
        std::vector<TransitionChoice> choices;
        const std::regex tuplePattern(R"(\(([\s\S]*?)\))");
        const std::regex toPattern(R"(to\s*:\s*(\d+))");
        const std::regex chancePattern(R"(chance\s*:\s*([0-9]*\.?[0-9]+))");
        const std::regex whenPattern("when\\s*:\\s*\"([^\"]+)\"");

        for (auto choiceIterator = std::sregex_iterator(choicesBlock.begin(), choicesBlock.end(), tuplePattern);
             choiceIterator != std::sregex_iterator();
             ++choiceIterator)
        {
            const auto tuple = (*choiceIterator)[1].str();
            std::smatch fieldMatch;

            if (! std::regex_search(tuple, fieldMatch, toPattern))
                continue;

            const auto to = juce::String(fieldMatch[1].str()).getIntValue();
            auto chance = 1.0;
            juce::String condition;

            if (std::regex_search(tuple, fieldMatch, chancePattern))
                chance = juce::String(fieldMatch[1].str()).getDoubleValue();

            if (std::regex_search(tuple, fieldMatch, whenPattern))
                condition = juce::String(fieldMatch[1].str()).trim();

            if (to > 0 && chance > 0.0)
                choices.push_back({ to, chance, condition });
        }

        return choices;
    };

    if (std::regex_search(source, blockMatch, std::regex(R"(~linear\s*=\s*\(([\s\S]*?)\);)")))
    {
        const auto block = blockMatch[1].str();
        const std::regex entryPattern(R"((\d+)\s*:\s*(\d+))");

        for (auto iterator = std::sregex_iterator(block.begin(), block.end(), entryPattern);
             iterator != std::sregex_iterator();
             ++iterator)
        {
            const auto from = juce::String((*iterator)[1].str()).getIntValue();
            const auto to = juce::String((*iterator)[2].str()).getIntValue();

            if (from > 0 && to > 0)
                rules.linear[from] = to;
        }
    }

    if (std::regex_search(source, blockMatch, std::regex(R"(~weighted\s*=\s*\(([\s\S]*?)\);)")))
    {
        const auto block = blockMatch[1].str();
        const std::regex statePattern(R"((\d+)\s*:\s*\[([\s\S]*?)\])");

        for (auto stateIterator = std::sregex_iterator(block.begin(), block.end(), statePattern);
             stateIterator != std::sregex_iterator();
             ++stateIterator)
        {
            const auto from = juce::String((*stateIterator)[1].str()).getIntValue();
            const auto choicesBlock = (*stateIterator)[2].str();
            auto choices = parseChoiceBlock(choicesBlock);

            if (from > 0 && ! choices.empty())
                rules.weighted[from] = std::move(choices);
        }
    }

    if (std::regex_search(source, blockMatch, std::regex(R"(~cycle\s*=\s*\[([\s\S]*?)\]\s*;)")))
    {
        const auto block = blockMatch[1].str();
        const std::regex numberPattern(R"(\d+)");

        for (auto iterator = std::sregex_iterator(block.begin(), block.end(), numberPattern);
             iterator != std::sregex_iterator();
             ++iterator)
        {
            const auto state = juce::String((*iterator)[0].str()).getIntValue();

            if (state > 0)
                rules.cycle.push_back(state);
        }
    }

    if (std::regex_search(source, blockMatch, std::regex(R"(~trigger\s*=\s*\(([\s\S]*?)\);)")))
    {
        const auto block = blockMatch[1].str();
        const std::regex weightedTriggerPattern(R"(([A-Za-z_][A-Za-z0-9_-]*)\s*:\s*\[([\s\S]*?)\])");
        const std::regex simpleTriggerPattern(R"(([A-Za-z_][A-Za-z0-9_-]*)\s*:\s*(\d+))");

        for (auto iterator = std::sregex_iterator(block.begin(), block.end(), weightedTriggerPattern);
             iterator != std::sregex_iterator();
             ++iterator)
        {
            const auto name = juce::String((*iterator)[1].str()).trim().toLowerCase();
            auto choices = parseChoiceBlock((*iterator)[2].str());

            if (name.isNotEmpty() && ! choices.empty())
                rules.triggers[name] = std::move(choices);
        }

        const auto simpleOnlyBlock = std::regex_replace(block, weightedTriggerPattern, "");

        for (auto iterator = std::sregex_iterator(simpleOnlyBlock.begin(), simpleOnlyBlock.end(), simpleTriggerPattern);
             iterator != std::sregex_iterator();
             ++iterator)
        {
            const auto name = juce::String((*iterator)[1].str()).trim().toLowerCase();
            const auto target = juce::String((*iterator)[2].str()).getIntValue();

            if (name.isNotEmpty() && target > 0 && rules.triggers.find(name) == rules.triggers.end())
                rules.triggers[name] = { { target, 1.0, {} } };
        }
    }

    return rules;
}

bool MainComponent::transitionConditionMatches(juce::String condition, const TransitionContext& context) const
{
    condition = condition.trim();
    const auto normalized = condition.toLowerCase();

    if (condition.isEmpty() || normalized == "true")
        return true;

    if (normalized == "false")
        return false;

    if (condition.startsWithIgnoreCase("trigger"))
    {
        const auto equals = condition.indexOf("==");
        if (equals < 0)
            return false;

        auto wanted = condition.substring(equals + 2).trim();

        if (wanted.length() >= 2 && wanted.startsWithChar('"') && wanted.endsWithChar('"'))
            wanted = wanted.substring(1, wanted.length() - 1);

        wanted = wanted.toLowerCase();
        return wanted == context.triggerName.toLowerCase();
    }

    const auto source = condition.toStdString();
    const std::regex comparisonPattern(R"((state|tick|beat|bar|frame)\s*(?:%\s*(\d+)\s*)?(==|!=|>=|<=|>|<)\s*(-?\d+))");
    std::smatch match;

    if (! std::regex_match(source, match, comparisonPattern))
        return false;

    const auto name = juce::String(match[1].str());
    auto left = 0LL;

    if (name == "state")
        left = context.state;
    else if (name == "tick")
        left = static_cast<long long>(context.stateFrame);
    else if (name == "frame")
        left = static_cast<long long>(context.frame);
    else if (name == "beat")
        left = context.beat;
    else if (name == "bar")
        left = context.bar;

    const auto modulo = juce::String(match[2].str()).getLargeIntValue();
    if (modulo > 0)
        left %= modulo;

    const auto op = juce::String(match[3].str());
    const auto right = juce::String(match[4].str()).getLargeIntValue();

    if (op == "==") return left == right;
    if (op == "!=") return left != right;
    if (op == ">=") return left >= right;
    if (op == "<=") return left <= right;
    if (op == ">")  return left > right;
    if (op == "<")  return left < right;

    return false;
}

int MainComponent::chooseTransitionTarget(const TransitionRules& rules, const int currentState, const TransitionContext& context)
{
    auto chooseFromChoices = [this, &context](const std::vector<TransitionChoice>& choices)
    {
        double total = 0.0;
        int lastMatchingTarget = 0;

        for (const auto& choice : choices)
        {
            if (transitionConditionMatches(choice.condition, context))
            {
                total += juce::jmax(0.0, choice.chance);
                lastMatchingTarget = choice.targetState;
            }
        }

        if (total <= 0.0)
            return 0;

        const auto roll = transitionRandom.nextDouble() * total;
        double sum = 0.0;

        for (const auto& choice : choices)
        {
            if (! transitionConditionMatches(choice.condition, context))
                continue;

            sum += juce::jmax(0.0, choice.chance);

            if (roll <= sum)
                return choice.targetState;
        }

        return lastMatchingTarget;
    };

    if (context.triggerName.isNotEmpty())
    {
        if (const auto trigger = rules.triggers.find(context.triggerName.toLowerCase()); trigger != rules.triggers.end())
        {
            const auto target = chooseFromChoices(trigger->second);
            if (target > 0)
                return target;
        }
    }

    if (const auto weighted = rules.weighted.find(currentState); weighted != rules.weighted.end())
    {
        const auto target = chooseFromChoices(weighted->second);
        if (target > 0)
            return target;
    }

    if (const auto linear = rules.linear.find(currentState); linear != rules.linear.end())
        return linear->second;

    if (! rules.cycle.empty())
    {
        for (int index = 0; index < static_cast<int>(rules.cycle.size()); ++index)
        {
            if (rules.cycle[static_cast<std::size_t>(index)] == currentState)
                return rules.cycle[static_cast<std::size_t>((index + 1) % static_cast<int>(rules.cycle.size()))];
        }
    }

    return 0;
}

void MainComponent::applyTransitionTarget(const int targetState)
{
    const auto targetIndex = targetState - 1;
    double stateBpm = transportEngine.getBpm();

    if (targetIndex < 0)
        return;

    storeActiveGridSlot();

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (targetIndex >= static_cast<int>(compositionStates.size()))
            return;

        if (targetIndex == activeStateIndex)
            return;

        activeStateIndex = targetIndex;
        activeStateEntryFrame = lastTransportFrame;
        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

        if (state.grids.empty())
            state.grids.push_back({ makeEmptyGridSnapshot() });

        activeGridSlot = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
        stateBpm = state.bpm;

        for (auto& grid : state.grids)
            grid.lastEvaluatedFrame = std::numeric_limits<std::uint64_t>::max();

        gridModel.applySnapshot(state.grids[static_cast<std::size_t>(activeGridSlot)].snapshot);
    }

    transportEngine.setBpm(stateBpm);
    gridEditor.clearPlayhead();
    gridEditor.fitToView();
    updateTransportControls();
    updateGridSlotControls();
    updateStateAdvanceControls();
    showActiveTransitionCode();
    statusLog.append("Transition -> state " + juce::String(targetState));
    repaint();
}

bool MainComponent::applyTransitionTargetForTransport(const int targetState,
                                                      const std::uint64_t transitionFrame,
                                                      double& stateBpmOut)
{
    const auto targetIndex = targetState - 1;

    if (targetIndex < 0)
        return false;

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (targetIndex >= static_cast<int>(compositionStates.size()))
            return false;

        if (targetIndex == activeStateIndex)
        {
            activeStateEntryFrame = transitionFrame;
            stateBpmOut = compositionStates[static_cast<std::size_t>(activeStateIndex)].bpm;
            return false;
        }

        activeStateIndex = targetIndex;
        activeStateEntryFrame = transitionFrame;

        auto& state = compositionStates[static_cast<std::size_t>(activeStateIndex)];

        if (state.grids.empty())
            state.grids.push_back({ makeEmptyGridSnapshot() });

        activeGridSlot = juce::jlimit(0, static_cast<int>(state.grids.size()) - 1, activeGridSlot);
        stateBpmOut = state.bpm;

        for (auto& grid : state.grids)
            grid.lastEvaluatedFrame = std::numeric_limits<std::uint64_t>::max();
    }

    if (! exportInProgress.load(std::memory_order_acquire))
    {
        pendingTransitionUiState.store(targetIndex, std::memory_order_release);
        pendingTransitionUiRefresh.store(true, std::memory_order_release);
    }

    return true;
}

void MainComponent::advanceStateFromTransitionPane(const TransportEngine::TickResult& result)
{
    if (result.context.frame == 0)
        return;

    CompositionState::AdvanceMode mode = CompositionState::AdvanceMode::manual;
    int currentState = 1;
    std::uint64_t stateEntryFrame = 0;
    int timeSignatureNumerator = 4;
    int timeSignatureDenominator = 4;
    juce::String transitionCode;

    {
        const std::lock_guard lock(gridRuntimeMutex);

        if (compositionStates.empty())
            return;

        if (compositionStates.size() <= 1)
            return;

        const auto stateIndex = juce::jlimit(0, static_cast<int>(compositionStates.size()) - 1, activeStateIndex);
        const auto& state = compositionStates[static_cast<std::size_t>(stateIndex)];
        currentState = stateIndex + 1;
        stateEntryFrame = activeStateEntryFrame;
        mode = state.advanceMode;
        timeSignatureNumerator = juce::jlimit(1, 32, state.timeSignatureNumerator);
        timeSignatureDenominator = juce::jlimit(1, 32, state.timeSignatureDenominator);
        transitionCode = state.transitionCode;
    }

    bool shouldAdvance = false;
    const auto rules = parseTransitionRules(transitionCode);
    TransitionContext transitionContext;
    transitionContext.frame = result.context.frame;
    transitionContext.state = currentState;
    transitionContext.stateFrame = result.context.frame >= stateEntryFrame ? result.context.frame - stateEntryFrame : 0;
    transitionContext.beat = static_cast<int>(transitionContext.stateFrame / static_cast<std::uint64_t>(juce::jmax(1, result.context.ticksPerBeat)));
    {
        const auto quarterBeatsPerBar = static_cast<double>(timeSignatureNumerator) * 4.0
                                        / static_cast<double>(juce::jmax(1, timeSignatureDenominator));
        const auto framesPerBar = static_cast<double>(juce::jmax(1, result.context.ticksPerBeat)) * quarterBeatsPerBar;
        transitionContext.bar = framesPerBar > 0.0
                                    ? static_cast<int>(std::floor(static_cast<double>(transitionContext.stateFrame) / framesPerBar))
                                    : 0;
    }

    switch (mode)
    {
        case CompositionState::AdvanceMode::manual:
            return;

        case CompositionState::AdvanceMode::beats:
        case CompositionState::AdvanceMode::bars:
            return;

        case CompositionState::AdvanceMode::trigger:
        {
            for (const auto& event : result.evaluation.events)
            {
                if (const auto* trigger = std::get_if<TriggerEvent>(&event))
                {
                    const auto name = trigger->triggerName.trim().toLowerCase();
                    const auto hasNamedRoute = rules.triggers.find(name) != rules.triggers.end();

                    if (name == "advance" || name == "next" || name == "transition" || hasNamedRoute)
                    {
                        transitionContext.triggerName = name;
                        shouldAdvance = true;
                        break;
                    }
                }
            }

            break;
        }
    }

    if (! shouldAdvance)
        return;

    if (rules.linear.empty() && rules.weighted.empty() && rules.cycle.empty() && rules.triggers.empty())
        return;

    const auto targetState = chooseTransitionTarget(rules, currentState, transitionContext);

    if (targetState > 0 && targetState != currentState)
    {
        double stateBpm = result.context.bpm;

        if (applyTransitionTargetForTransport(targetState, result.context.frame, stateBpm))
            transportEngine.setBpm(stateBpm);
    }
    else if (targetState == currentState)
    {
        const std::lock_guard lock(gridRuntimeMutex);
        activeStateEntryFrame = result.context.frame;
    }
}

void MainComponent::handleTransportTick(const TransportEngine::TickResult& result)
{
    lastTransportFrame = result.context.frame;
    lastTickInBeat = result.context.tickInBeat;
    lastTickWasBeat = result.context.isBeat;
    lastPulseTimeMs = juce::Time::getMillisecondCounterHiRes();

    if (gridModel.getHeight() > 0)
        gridEditor.setPlayheadRow(static_cast<int>(getDisplayGridFrame(result.context.frame) % static_cast<std::uint64_t>(gridModel.getHeight())));

    for (const auto& event : result.evaluation.events)
    {
        if (const auto* mutation = std::get_if<GridMutationEvent>(&event))
            gridEditor.repaintRow(mutation->targetCell.row);
    }

    appendEvaluatedEventsToLog(result);

    if (pendingTransitionUiRefresh.exchange(false, std::memory_order_acq_rel))
    {
        const auto stateIndex = pendingTransitionUiState.exchange(-1, std::memory_order_acq_rel);
        juce::ignoreUnused(stateIndex);
        gridEditor.clearPlayhead();
        showActiveLane();
        updateTransportControls();
        updateGridSlotControls();
        updateStateAdvanceControls();
        showActiveTransitionCode();
        statusLog.append("Transition -> state " + juce::String(activeStateIndex + 1));
    }

    refreshStateGraph();
    refreshArrangementView();
    repaint(0, 0, getWidth(), 92);
    repaint(0, juce::jmax(0, getHeight() - 76), getWidth(), 76);
}

void MainComponent::appendEvaluatedEventsToLog(const TransportEngine::TickResult& result)
{
    const auto routedLogs = eventRouter.route(result.evaluation.events, result.evaluation.grid, oscOutput);

    if (routedLogs.empty())
    {
        if (result.context.isBeat)
            statusLog.append("Tick " + juce::String(result.context.frame) + ": no events");
        return;
    }

    juce::String message = "Tick " + juce::String(result.context.frame) + ": ";
    const auto eventsToShow = juce::jmin(3, static_cast<int>(routedLogs.size()));

    for (int index = 0; index < eventsToShow; ++index)
    {
        if (index > 0)
            message += " | ";

        message += routedLogs[static_cast<std::size_t>(index)].message;
    }

    for (const auto& event : routedLogs)
        appendEventMonitorLine(event);

    if (static_cast<int>(routedLogs.size()) > eventsToShow)
        message += " | +" + juce::String(static_cast<int>(routedLogs.size()) - eventsToShow);

    statusLog.append(message);
}

void MainComponent::stopTransport()
{
    transportEngine.stop();
    gridEditor.clearPlayhead();
    statusLog.append("Transport stopped");
    updateTransportControls();
    refreshStateGraph();
    repaint();
}

juce::String MainComponent::getTransportStateText() const
{
    juce::String state;

    switch (transportEngine.getState())
    {
        case TransportEngine::State::running: state = "PLAY"; break;
        case TransportEngine::State::paused:  state = "PAUSE"; break;
        case TransportEngine::State::stopped: state = "STOP"; break;
    }

    return state
        + "  " + juce::String(transportEngine.getBpm(), 1) + " BPM"
        + "  TPB " + juce::String(transportEngine.getTicksPerBeat())
        + "  TICK " + juce::String(lastTickInBeat + 1)
        + "  FRAME " + juce::String(lastTransportFrame).paddedLeft('0', 4);
}
}
