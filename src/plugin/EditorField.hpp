#pragma once
// Module field of the single-screen editor: eleven typed slots plus the OUT bus at the #69 positions, preallocated
// IN/OUT ports with drag-to-connect, and cables routed by the #69 router. Patch nodes map onto slots by a stable
// id convention (`src1`, `res2`, `filt1`, `shape2`, `mix1`; see assignSlots) and otherwise by patch order; no
// free-form placement and no saved layout exists.
#include "EditorControls.hpp"
#include "iupac/engine/Engine.hpp"
#include <map>
#include <tuple>
#include <memory>
#include <string>
#include <utility>
namespace iupac::ui
{
inline constexpr std::size_t moduleSlotCount=outputSlot;// the sixteen typed slots precede the OUT bus row
struct SlotMap{std::array<int,slotCount>node{};std::array<int,domain::maximumNodes>slot{};SlotMap(){node.fill(-1);slot.fill(-1);}};
[[nodiscard]]SlotMap assignSlots(const domain::Patch&);
[[nodiscard]]std::string slotNodeId(std::size_t slot);// canonical id for a node authored into `slot`
[[nodiscard]]std::string uniqueNodeId(const domain::Patch&,std::size_t slot);
[[nodiscard]]bool slotAccepts(std::size_t slot,domain::ModuleType);
[[nodiscard]]domain::Node defaultNode(std::string id,const domain::ModuleDescriptor&);
class ModuleField;
class PortView final:public juce::Component
{
public:
 // `modulation` selects the D8 typed audio-rate anchor (#127) instead of the ordinary IN anchor.
 PortView(ModuleField&,std::size_t slot,bool input,bool modulation=false);void paint(juce::Graphics&)override;
 void mouseEnter(const juce::MouseEvent&)override;void mouseExit(const juce::MouseEvent&)override;void mouseDown(const juce::MouseEvent&)override;void mouseDrag(const juce::MouseEvent&)override;void mouseUp(const juce::MouseEvent&)override;
 [[nodiscard]]std::size_t slot()const noexcept{return slot_;}[[nodiscard]]bool isInput()const noexcept{return input_;}[[nodiscard]]bool isModulation()const noexcept{return modulation_;}
 enum class State{idle,highlighted,legal,inert};void setState(State s){if(state_!=s){state_=s;repaint();}}
private:
 ModuleField&field_;std::size_t slot_;bool input_,modulation_;State state_{State::idle};
};
class SlotView final:public juce::Component
{
public:
 SlotView(ModuleField&,std::size_t slot);void paint(juce::Graphics&)override;void resized()override;void mouseDown(const juce::MouseEvent&)override;void mouseUp(const juce::MouseEvent&)override;
 // Rebuilds the control kit when the node/type changes and otherwise syncs values; nullptr deactivates the view.
 void setNode(const domain::Node*);[[nodiscard]]const std::string&nodeId()const noexcept{return nodeId_;}[[nodiscard]]bool active()const noexcept{return !nodeId_.empty();}
 void setEffective(const std::array<float,engine::parameterTargetCount>*values,const std::vector<std::string>&targetedParameters);
 [[nodiscard]]ValueControl*control(std::string_view parameter)const;[[nodiscard]]Forest*forest(std::string_view parameter)const;[[nodiscard]]SegmentToggle*toggle(std::string_view parameter)const;
 void setHighlightedParameter(std::string_view parameter);
private:
 void build(const domain::Node&);void sync(const domain::Node&);void openTable();void commit(std::string_view parameter,std::vector<double>values);
 ModuleField&field_;std::size_t slot_;std::string nodeId_;domain::ModuleType type_{};std::vector<std::pair<std::string,std::unique_ptr<juce::Component>>>controls_;std::unique_ptr<GlyphButton>remove_,edit_;int mode_{};bool pressed_{};
};
class CableLayer final:public juce::Component
{
public:
 explicit CableLayer(ModuleField&);void paint(juce::Graphics&)override;bool hitTest(int,int)override;
 void mouseMove(const juce::MouseEvent&)override;void mouseExit(const juce::MouseEvent&)override;void mouseDown(const juce::MouseEvent&)override;void mouseDrag(const juce::MouseEvent&)override;void mouseUp(const juce::MouseEvent&)override;
 struct Drawn{Cable cable;std::vector<juce::Point<float>>points;juce::Path path;juce::Colour colour;domain::AudioEdge edge;std::size_t sourceSlot{},destinationSlot{};};
 void setCables(std::vector<Drawn>);[[nodiscard]]const std::vector<Drawn>&cables()const noexcept{return cables_;}[[nodiscard]]int hovered()const noexcept{return hovered_;}
 // Gain knobs are keyed by their edge and reconciled in place, so an ordinary gain edit never destroys the
 // component a gesture is running on (#87); only a topology change adds or removes one.
 // Keyed by port as well: a resonator can be fed on both its `in` and its `exciteIn` by the same
 // source, and those are two distinct edges with two distinct gain knobs (#127).
 using EdgeKey=std::tuple<std::string,std::string,int>;
 [[nodiscard]]Knob*gainKnob(std::string_view source,std::string_view destination,domain::AudioPort port=domain::AudioPort::in)const;
private:
 [[nodiscard]]int cableAt(juce::Point<float>,float tolerance)const noexcept;
 ModuleField&field_;std::vector<Drawn>cables_;std::map<EdgeKey,std::unique_ptr<Knob>>knobs_;int hovered_{-1};
};
class ModuleField final:public juce::Component
{
public:
 ModuleField();~ModuleField()override;void paint(juce::Graphics&)override;void paintOverChildren(juce::Graphics&)override;void resized()override;
 void setPatch(const domain::Patch&);[[nodiscard]]const domain::Patch&patch()const noexcept{return patch_;}[[nodiscard]]const SlotMap&slotMap()const noexcept{return map_;}
 void setEffective(const engine::EffectiveValues&);
 [[nodiscard]]SlotView&slot(std::size_t i){return*slots_[i];}[[nodiscard]]PortView*inputPort(std::size_t slot)const;[[nodiscard]]PortView*modulationPort(std::size_t slot)const;[[nodiscard]]PortView*outputPort(std::size_t slot)const;[[nodiscard]]CableLayer&cables()noexcept{return*cables_;}
 [[nodiscard]]juce::Rectangle<float>slotBounds(std::size_t slot)const;[[nodiscard]]juce::Point<float>toWindow(Point)const;
 void setHighlightedParameter(std::string_view nodeId,std::string_view parameter);
 // Editing callbacks: every mutation goes through the owner's document transaction and the field is re-synced from the result.
 std::function<void(std::string_view type,std::size_t slot)>onActivate;std::function<void(std::size_t slot)>onDeactivate;
 std::function<void(std::string source,std::string destination,double gain,domain::AudioPort port)>onConnect;std::function<void(std::string source,std::string destination,domain::AudioPort port)>onDisconnect;std::function<void(std::string source,std::string destination,double gain,domain::AudioPort port)>onGain;
 std::function<void(std::string nodeId,std::string parameter,std::vector<double>)>onParameter;
 // Drag state shared by ports and cables.
 enum class DragKind{none,connect,detachInput,detachOutput};
 void beginConnect(std::size_t sourceSlot);void beginDetach(std::size_t cableIndex,bool inputEnd);void updateDrag(juce::Point<float>fieldPosition);void endDrag(juce::Point<float>fieldPosition);[[nodiscard]]DragKind dragKind()const noexcept{return drag_;}
 [[nodiscard]]bool edgeLegal(std::string_view source,std::string_view destination,domain::AudioPort port=domain::AudioPort::in)const;[[nodiscard]]std::string destinationId(std::size_t slot)const;
 // The typed IN port a slot's current node declares, or `in` when the drag targets the ordinary
 // anchor. A slot holding a type without one (a harmonic in a source slot) reports `in`, which is
 // never a legal target for the modulation anchor, so the anchor stays inert (#127).
 [[nodiscard]]domain::AudioPort slotPort(std::size_t slot,bool modulation)const;
private:
 void routeCables();[[nodiscard]]PortView*portAt(juce::Point<float>)const;
 domain::Patch patch_;SlotMap map_;std::vector<std::unique_ptr<SlotView>>slots_;std::vector<std::unique_ptr<PortView>>ports_;std::unique_ptr<CableLayer>cables_;
 DragKind drag_{DragKind::none};std::size_t dragSource_{},dragCable_{};juce::Point<float>dragPoint_;PortView*dragTarget_{};
 // Legality is per anchor, not per slot: a resonator's `in` and its `exciteIn` can differ (#127).
 std::array<std::array<bool,2>,slotCount>legal_{};[[nodiscard]]bool&legalFor(const PortView&p){return legal_[p.slot()][p.isModulation()?1:0];}[[nodiscard]]bool legalFor(const PortView&p)const{return legal_[p.slot()][p.isModulation()?1:0];}
};
}
