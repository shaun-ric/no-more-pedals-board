#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Core/Graph.h"
#include "PlayView.h"

// What is selected in the graph view. The settings panel shows only this.
struct GraphSelection
{
    std::string node;       // a node's ID, or empty
    std::string from, to;   // a connection, or empty

    bool isNode() const { return ! node.empty(); }
    bool isEdge() const { return ! from.empty(); }
    bool operator== (const GraphSelection& o) const { return node == o.node && from == o.from && to == o.to; }
    bool operator!= (const GraphSelection& o) const { return ! (*this == o); }
};

// A picture of the scene's signal path: boxes for nodes, wires for connections, arranged in columns
// from the inputs on the left to the outputs on the right. The layout is automatic (it is worked out
// from the connections each time the graph changes), so nothing about it is saved.
//
//  - Click a node or a wire to select it. The settings panel then shows only that item.
//  - Click the round power button on a node to switch it on or off.
//  - Drag from the dot on a node's right edge to another node's left edge to connect them.
//
// Drag a box by its body to place it; the place is saved with the scene. Boxes without a saved place
// are arranged automatically in columns.
//
// Dragging a connection's end: drag from the arrow end of a wire (a node's left dot) to move it to
// another node, or into empty space to break it. Shift-drag from a node's right dot picks up the source
// end of a wire in the same way (the selected wire, or the node's only outgoing one).
//
// Bright wires carry signal (the connection is switched on), grey dashed wires are switched off, and a
// wire's brightness follows its level.
class GraphView : public juce::Component
{
public:
    std::function<void (const GraphSelection&)> onSelect;
    std::function<void (const std::string& nodeId)> onToggleNode;
    std::function<void (const std::string& from, const std::string& to)> onConnect;
    std::function<void (const std::string& from, const std::string& to)> onDisconnect;
    // A box was dragged somewhere: its centre as a fraction of the view's width and height.
    std::function<void (const std::string& nodeId, float x, float y)> onMoveNode;
    // A connection's end was dragged onto a different node: the old connection is replaced by the new one.
    std::function<void (const std::string& oldFrom, const std::string& oldTo,
                        const std::string& newFrom, const std::string& newTo)> onReconnect;

    // Takes a snapshot of the graph. Call it whenever the graph's revision changes.
    // `positions` holds the boxes that have a saved place (node ID -> centre, as fractions of the view).
    void setGraph (const Graph& g, const std::map<std::string, juce::Point<float>>& positions = {})
    {
        nodes = g.nodes();
        edges = g.edges();
        saved = positions;
        dragKind = Drag::None;   // indices into the old lists mean nothing now
        dragFrom = dragEdge = -1;
        layoutNodes();

        if (selection.isNode() && indexOf (selection.node) < 0)
            selection = {};

        if (selection.isEdge() && ! hasEdge (selection.from, selection.to))
            selection = {};

        repaint();
    }

    void setSelection (const GraphSelection& s) { selection = s; repaint(); }
    const GraphSelection& getSelection() const { return selection; }

    // Called from the UI timer, so states changed by MIDI show up.
    void refresh() { repaint(); }

    void resized() override { layoutNodes(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff15171a));

        if (nodes.empty())
        {
            g.setColour (juce::Colour (0xff8a8f96));
            g.setFont (16.0f);
            g.drawText ("This scene has no nodes. Use Add node... to start.", getLocalBounds(), juce::Justification::centred);
            return;
        }

        // Wires first, so the boxes sit on top of them.
        for (size_t k = 0; k < edges.size(); ++k)
        {
            const auto& e = edges[k];
            const int a = indexOf (e.from), b = indexOf (e.to);
            if (a < 0 || b < 0 || (int) k == dragEdge)
                continue;

            const auto path = wirePath (a, b);
            const bool on = e.enabled != nullptr && e.enabled->on();
            const bool selected = selection.isEdge() && selection.from == e.from && selection.to == e.to;

            if (selected)
            {
                g.setColour (juce::Colours::white.withAlpha (0.35f));
                g.strokePath (path, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            if (on)
            {
                const float levelDb = e.level != nullptr ? e.level->get() : 0.0f;
                const float alpha = juce::jmap (juce::jlimit (-40.0f, 0.0f, levelDb), -40.0f, 0.0f, 0.3f, 1.0f);
                g.setColour (PlayView::colourFor (*nodes[(size_t) a]).brighter (0.2f).withAlpha (alpha));
                g.strokePath (path, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            else
            {
                juce::Path dashed;
                const float dashes[] = { 6.0f, 5.0f };
                juce::PathStrokeType (1.6f).createDashedStroke (dashed, path, dashes, 2);
                g.setColour (juce::Colour (0xff5b6066));
                g.fillPath (dashed);
            }

            // Arrow head at the target end.
            const auto end = inputPoint (b);
            juce::Path arrow;
            arrow.addTriangle (end.x, end.y, end.x - 9.0f, end.y - 5.0f, end.x - 9.0f, end.y + 5.0f);
            g.setColour (on ? PlayView::colourFor (*nodes[(size_t) a]).brighter (0.2f) : juce::Colour (0xff5b6066));
            g.fillPath (arrow);
        }

        // A connection being dragged out, or the end of an existing one being moved.
        if (dragKind != Drag::None)
        {
            const bool movingSource = dragKind == Drag::MoveSource;
            juce::Point<float> fixedEnd;

            if (dragKind == Drag::NewConnection)
                fixedEnd = outputPoint (dragFrom);
            else if (movingSource)
                fixedEnd = inputPoint (indexOf (edges[(size_t) dragEdge].to));
            else
                fixedEnd = outputPoint (indexOf (edges[(size_t) dragEdge].from));

            const auto a = movingSource ? dragPoint : fixedEnd;
            const auto b = movingSource ? fixedEnd : dragPoint;
            const float dx = juce::jmax (30.0f, std::abs (b.x - a.x) * 0.5f);

            juce::Path p;
            p.startNewSubPath (a);
            p.cubicTo (a.x + dx, a.y, b.x - dx, b.y, b.x, b.y);

            // Red when letting go here would break an existing connection.
            const bool breaking = dragKind != Drag::NewConnection && nodeUnder (dragPoint) < 0;
            g.setColour ((breaking ? juce::Colour (0xffff6b5a) : juce::Colours::white).withAlpha (0.85f));
            g.strokePath (p, juce::PathStrokeType (2.5f));
        }

        for (size_t i = 0; i < nodes.size(); ++i)
            paintNode (g, (int) i);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = e.position;
        dragKind = Drag::None;
        dragEdge = -1;
        moveNode = -1;

        for (int i = (int) nodes.size() - 1; i >= 0; --i)
        {
            const auto r = rects[(size_t) i];
            const auto& id = nodes[(size_t) i]->id();

            if (! nodes[(size_t) i]->isOutput() && outputPoint (i).getDistanceFrom (p) <= 10.0f)
            {
                if (e.mods.isShiftDown())
                {
                    const int k = pickEdge (id, true);
                    if (k >= 0)
                    {
                        dragKind = Drag::MoveSource;
                        dragEdge = k;
                        dragPoint = p;
                        select ({ {}, edges[(size_t) k].from, edges[(size_t) k].to });
                        return;
                    }
                }

                dragKind = Drag::NewConnection;
                dragFrom = i;
                dragPoint = p;
                return;
            }

            if (! nodes[(size_t) i]->isInput() && inputPoint (i).getDistanceFrom (p) <= 10.0f)
            {
                const int k = pickEdge (id, false);
                if (k >= 0)
                {
                    dragKind = Drag::MoveTarget;
                    dragEdge = k;
                    dragPoint = p;
                    select ({ {}, edges[(size_t) k].from, edges[(size_t) k].to });
                    return;
                }
            }

            if (powerPoint (i).getDistanceFrom (p) <= 11.0f)
            {
                if (onToggleNode)
                    onToggleNode (nodes[(size_t) i]->id());
                return;
            }

            if (r.contains (p))
            {
                select ({ nodes[(size_t) i]->id(), {}, {} });
                moveNode = i;
                moveOffset = p - r.getCentre();
                moved = false;
                return;
            }
        }

        // A wire?
        float best = 8.0f;
        int bestEdge = -1;

        for (size_t k = 0; k < edges.size(); ++k)
        {
            const int a = indexOf (edges[k].from), b = indexOf (edges[k].to);
            if (a < 0 || b < 0)
                continue;

            const float d = distanceToWire (a, b, p);
            if (d < best)
            {
                best = d;
                bestEdge = (int) k;
            }
        }

        if (bestEdge >= 0)
            select ({ {}, edges[(size_t) bestEdge].from, edges[(size_t) bestEdge].to });
        else
            select ({});
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (moveNode >= 0 && moveNode < (int) nodes.size())
        {
            if (! moved && e.getDistanceFromDragStart() < 5)
                return;

            moved = true;
            const auto centre = e.position - moveOffset;
            saved[nodes[(size_t) moveNode]->id()] = { juce::jlimit (0.03f, 0.97f, centre.x / (float) juce::jmax (1, getWidth())),
                                                      juce::jlimit (0.05f, 0.95f, centre.y / (float) juce::jmax (1, getHeight())) };
            layoutNodes();
            repaint();
            return;
        }

        if (dragKind != Drag::None)
        {
            dragPoint = e.position;
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (moveNode >= 0)
        {
            const int i = moveNode;
            moveNode = -1;

            if (moved && i < (int) nodes.size() && onMoveNode)
            {
                const auto it = saved.find (nodes[(size_t) i]->id());
                if (it != saved.end())
                    onMoveNode (nodes[(size_t) i]->id(), it->second.x, it->second.y);
            }
            return;
        }

        if (dragKind == Drag::None)
            return;

        const Drag kind = dragKind;
        const int from = dragFrom, edgeIndex = dragEdge;
        dragKind = Drag::None;
        dragFrom = -1;
        dragEdge = -1;
        repaint();

        const int under = nodeUnder (e.position);

        if (kind == Drag::NewConnection)
        {
            if (under >= 0 && under != from && ! nodes[(size_t) under]->isInput() && onConnect)
                onConnect (nodes[(size_t) from]->id(), nodes[(size_t) under]->id());
            return;
        }

        if (edgeIndex < 0 || edgeIndex >= (int) edges.size())
            return;

        const std::string oldFrom = edges[(size_t) edgeIndex].from, oldTo = edges[(size_t) edgeIndex].to;

        if (under < 0)   // let go in empty space: break the connection
        {
            if (onDisconnect)
                onDisconnect (oldFrom, oldTo);
            return;
        }

        const std::string& target = nodes[(size_t) under]->id();

        if (kind == Drag::MoveTarget)
        {
            if (target != oldTo && target != oldFrom && ! nodes[(size_t) under]->isInput() && onReconnect)
                onReconnect (oldFrom, oldTo, oldFrom, target);
        }
        else   // MoveSource
        {
            if (target != oldFrom && target != oldTo && ! nodes[(size_t) under]->isOutput() && onReconnect)
                onReconnect (oldFrom, oldTo, target, oldTo);
        }
    }

private:
    enum class Drag { None, NewConnection, MoveTarget, MoveSource };

    // The node whose box is under `p` (a little generous), or -1.
    int nodeUnder (juce::Point<float> p) const
    {
        for (size_t i = 0; i < nodes.size(); ++i)
            if (rects[i].expanded (6.0f).contains (p))
                return (int) i;

        return -1;
    }

    // Which of a node's incoming (or outgoing) connections an end-drag picks up: the selected one if it
    // belongs to this node, otherwise the first.
    int pickEdge (const std::string& nodeId, bool outgoing) const
    {
        int first = -1;

        for (size_t k = 0; k < edges.size(); ++k)
        {
            if ((outgoing ? edges[k].from : edges[k].to) != nodeId)
                continue;

            if (selection.isEdge() && selection.from == edges[k].from && selection.to == edges[k].to)
                return (int) k;

            if (first < 0)
                first = (int) k;
        }

        return first;
    }

    void select (const GraphSelection& s)
    {
        selection = s;
        repaint();

        if (onSelect)
            onSelect (s);
    }

    int indexOf (const std::string& id) const
    {
        for (size_t i = 0; i < nodes.size(); ++i)
            if (nodes[i]->id() == id)
                return (int) i;

        return -1;
    }

    bool hasEdge (const std::string& from, const std::string& to) const
    {
        for (const auto& e : edges)
            if (e.from == from && e.to == to)
                return true;

        return false;
    }

    juce::Point<float> outputPoint (int i) const { return { rects[(size_t) i].getRight(), rects[(size_t) i].getCentreY() }; }
    juce::Point<float> inputPoint (int i) const { return { rects[(size_t) i].getX(), rects[(size_t) i].getCentreY() }; }
    juce::Point<float> powerPoint (int i) const { return { rects[(size_t) i].getRight() - 15.0f, rects[(size_t) i].getY() + 15.0f }; }

    juce::Path wirePath (int a, int b) const
    {
        const auto p0 = outputPoint (a), p1 = inputPoint (b);
        const float dx = juce::jmax (30.0f, std::abs (p1.x - p0.x) * 0.5f);

        juce::Path p;
        p.startNewSubPath (p0);
        p.cubicTo (p0.x + dx, p0.y, p1.x - dx, p1.y, p1.x, p1.y);
        return p;
    }

    float distanceToWire (int a, int b, juce::Point<float> q) const
    {
        const auto p0 = outputPoint (a), p1 = inputPoint (b);
        const float dx = juce::jmax (30.0f, std::abs (p1.x - p0.x) * 0.5f);
        const juce::Point<float> c0 (p0.x + dx, p0.y), c1 (p1.x - dx, p1.y);

        float best = 1.0e9f;
        juce::Point<float> prev = p0;

        for (int s = 1; s <= 32; ++s)
        {
            const float t = (float) s / 32.0f, u = 1.0f - t;
            const juce::Point<float> pt (u * u * u * p0.x + 3 * u * u * t * c0.x + 3 * u * t * t * c1.x + t * t * t * p1.x,
                                         u * u * u * p0.y + 3 * u * u * t * c0.y + 3 * u * t * t * c1.y + t * t * t * p1.y);
            juce::Point<float> nearest;   // required by the call, not used
            best = juce::jmin (best, juce::Line<float> (prev, pt).getDistanceFromPoint (q, nearest));
            prev = pt;
        }

        return best;
    }

    void paintNode (juce::Graphics& g, int i) const
    {
        const auto& n = *nodes[(size_t) i];
        const auto r = rects[(size_t) i];
        const bool on = n.activeParam() != nullptr && n.activeParam()->on();
        const bool selected = selection.isNode() && selection.node == n.id();
        const auto base = PlayView::colourFor (n);

        g.setColour (on ? base : base.withSaturation (0.1f).darker (0.85f));
        g.fillRoundedRectangle (r, 9.0f);

        g.setColour (selected ? juce::Colours::white : (on ? juce::Colours::white.withAlpha (0.55f) : juce::Colour (0xff3a3d42)));
        g.drawRoundedRectangle (r.reduced (1.0f), 9.0f, selected ? 3.0f : 1.5f);

        const auto text = on ? juce::Colours::black : juce::Colour (0xff868b92);
        auto inner = r.reduced (9.0f, 5.0f);
        inner.removeFromRight (20.0f);   // room for the power button

        g.setColour (text);
        g.setFont (juce::FontOptions (juce::jlimit (12.0f, 16.0f, r.getHeight() * 0.3f), juce::Font::bold));
        g.drawFittedText (n.displayName(), inner.removeFromTop (inner.getHeight() * 0.55f).toNearestInt(),
                          juce::Justification::centredLeft, 1);

        g.setFont (juce::FontOptions (11.0f));
        juce::String sub = n.hasFile() ? (n.fileName().isNotEmpty() ? n.fileName() : juce::String ("(no file)"))
                                       : juce::String (n.type());
        g.drawFittedText (sub, inner.toNearestInt(), juce::Justification::centredLeft, 1);

        // Power button.
        const auto pp = powerPoint (i);
        g.setColour (on ? juce::Colours::black.withAlpha (0.75f) : juce::Colour (0xff4a4e54));
        g.fillEllipse (pp.x - 8.0f, pp.y - 8.0f, 16.0f, 16.0f);
        g.setColour (on ? juce::Colour (0xff7dff9a) : juce::Colour (0xff8a8f96));
        g.drawEllipse (pp.x - 5.0f, pp.y - 5.0f, 10.0f, 10.0f, 1.8f);
        g.drawLine (pp.x, pp.y - 6.0f, pp.x, pp.y - 1.0f, 1.8f);

        // Connection points.
        g.setColour (juce::Colour (0xffd5d8dc));
        if (! n.isInput())
            g.fillEllipse (inputPoint (i).x - 4.0f, inputPoint (i).y - 4.0f, 8.0f, 8.0f);
        if (! n.isOutput())
        {
            g.fillEllipse (outputPoint (i).x - 5.0f, outputPoint (i).y - 5.0f, 10.0f, 10.0f);
            g.setColour (juce::Colour (0xff15171a));
            g.fillEllipse (outputPoint (i).x - 2.0f, outputPoint (i).y - 2.0f, 4.0f, 4.0f);
        }
    }

    // Columns from the inputs (left) to the outputs (right). A node sits one column to the right of
    // the furthest-left node that feeds it. Within a column, nodes are ordered by where their sources are.
    void layoutNodes()
    {
        rects.assign (nodes.size(), {});
        if (nodes.empty() || getWidth() <= 0 || getHeight() <= 0)
            return;

        const size_t n = nodes.size();
        std::vector<int> rank (n, 0);

        for (size_t pass = 0; pass < n; ++pass)
        {
            bool changed = false;
            for (const auto& e : edges)
            {
                const int a = indexOf (e.from), b = indexOf (e.to);
                if (a >= 0 && b >= 0 && rank[(size_t) b] < rank[(size_t) a] + 1)
                {
                    rank[(size_t) b] = rank[(size_t) a] + 1;
                    changed = true;
                }
            }
            if (! changed)
                break;
        }

        int maxOther = 0;
        bool anyOther = false;
        for (size_t i = 0; i < n; ++i)
            if (! nodes[i]->isOutput())
            {
                maxOther = juce::jmax (maxOther, rank[i]);
                anyOther = true;
            }

        for (size_t i = 0; i < n; ++i)
            if (nodes[i]->isOutput())
                rank[i] = anyOther ? maxOther + 1 : 0;

        int maxRank = 0;
        for (const int r : rank)
            maxRank = juce::jmax (maxRank, r);

        std::vector<std::vector<int>> columns ((size_t) maxRank + 1);
        std::vector<float> row (n, 0.0f);

        for (int c = 0; c <= maxRank; ++c)
        {
            auto& col = columns[(size_t) c];
            for (size_t i = 0; i < n; ++i)
                if (rank[i] == c)
                    col.push_back ((int) i);

            // Barycentre of the sources' rows. Nodes with no sources keep their original order,
            // inputs ahead of anything else.
            std::vector<float> key (n, 0.0f);
            for (const int i : col)
            {
                float sum = 0.0f;
                int count = 0;
                for (const auto& e : edges)
                    if (e.to == nodes[(size_t) i]->id())
                    {
                        const int a = indexOf (e.from);
                        if (a >= 0 && rank[(size_t) a] < c)
                        {
                            sum += row[(size_t) a];
                            ++count;
                        }
                    }

                key[(size_t) i] = count > 0 ? sum / (float) count : (nodes[(size_t) i]->isInput() ? -1.0f : 1.0e6f + (float) i);
            }

            std::stable_sort (col.begin(), col.end(), [&key] (int a, int b) { return key[(size_t) a] < key[(size_t) b]; });

            for (size_t k = 0; k < col.size(); ++k)
                row[(size_t) col[k]] = (float) k;
        }

        const float margin = 14.0f;
        const float w = (float) getWidth() - 2 * margin, h = (float) getHeight() - 2 * margin;
        const float colW = w / (float) (maxRank + 1);
        const float boxW = juce::jlimit (96.0f, 190.0f, colW * 0.74f);

        for (int c = 0; c <= maxRank; ++c)
        {
            const auto& col = columns[(size_t) c];
            if (col.empty())
                continue;

            const float rowH = h / (float) col.size();
            const float boxH = juce::jlimit (32.0f, 62.0f, rowH - 8.0f);

            for (size_t k = 0; k < col.size(); ++k)
            {
                float cx = margin + colW * ((float) c + 0.5f);
                float cy = margin + rowH * ((float) k + 0.5f);

                const auto it = saved.find (nodes[(size_t) col[k]]->id());
                if (it != saved.end())
                {
                    cx = it->second.x * (float) getWidth();
                    cy = it->second.y * (float) getHeight();
                }

                rects[(size_t) col[k]] = { cx - boxW * 0.5f, cy - boxH * 0.5f, boxW, boxH };
            }
        }
    }

    std::vector<std::shared_ptr<Node>> nodes;
    std::vector<Graph::EdgeInfo> edges;
    std::vector<juce::Rectangle<float>> rects;
    GraphSelection selection;
    std::map<std::string, juce::Point<float>> saved;   // boxes with a place of their own
    int moveNode = -1;                                  // the box being dragged by its body
    juce::Point<float> moveOffset;
    bool moved = false;
    Drag dragKind = Drag::None;
    int dragFrom = -1;     // the node a new connection starts from
    int dragEdge = -1;     // the connection whose end is being moved
    juce::Point<float> dragPoint;
};
