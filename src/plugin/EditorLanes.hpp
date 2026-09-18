#pragma once
// Lane matrix: the vertically scrollable modulation-matrix stack beneath the module field. One lane per Patch matrix
// row — source picker · destination picker (active slot · eligible parameter) · bipolar depth bar · enable · remove.
// Lanes are the only element the user adds or removes; every edit is a document transaction owned by the editor.
#include "EditorField.hpp"
namespace iupac::ui
{
struct LaneDestination{std::string nodeId,parameter;juce::String label;};
[[nodiscard]]std::vector<LaneDestination>laneDestinations(const domain::Patch&);
[[nodiscard]]juce::String modulationSourceName(domain::ModulationSource);
class LaneMatrix;
class LaneView final:public juce::Component
{
public:
 LaneView(LaneMatrix&,std::size_t row);void resized()override;void paint(juce::Graphics&)override;void mouseDown(const juce::MouseEvent&)override;
 void sync(const domain::MatrixRow&,const std::vector<LaneDestination>&);[[nodiscard]]domain::MatrixRow row()const;void setSelected(bool s){if(selected_!=s){selected_=s;repaint();}}
 [[nodiscard]]juce::ComboBox&sourcePicker()noexcept{return source_;}[[nodiscard]]juce::ComboBox&destinationPicker()noexcept{return destination_;}[[nodiscard]]Fader&depthBar()noexcept{return depth_;}[[nodiscard]]juce::TextButton&enableButton()noexcept{return enable_;}[[nodiscard]]juce::Button&removeButton()noexcept{return remove_;}
private:
 void changed();
 LaneMatrix&matrix_;std::size_t row_;domain::MatrixRow value_;std::vector<LaneDestination>destinations_;juce::ComboBox source_,destination_;Fader depth_;juce::TextButton enable_{"on"};GlyphButton remove_{"remove"};bool selected_{},syncing_{};
};
class LaneMatrix final:public juce::Component
{
public:
 LaneMatrix();void resized()override;void paint(juce::Graphics&)override;
 void setPatch(const domain::Patch&);[[nodiscard]]std::size_t laneCount()const noexcept{return lanes_.size();}[[nodiscard]]LaneView&lane(std::size_t i){return*lanes_[i];}
 void select(std::optional<std::size_t>);[[nodiscard]]std::optional<std::size_t>selected()const noexcept{return selected_;}
 std::function<void()>onAdd;std::function<void(std::size_t,domain::MatrixRow)>onEdit;std::function<void(std::size_t)>onRemove;std::function<void(std::optional<std::size_t>)>onSelect;
 [[nodiscard]]juce::Button&addButton()noexcept{return add_;}
private:
 juce::TextButton add_{"+ route"};juce::Viewport viewport_;juce::Component content_;std::vector<std::unique_ptr<LaneView>>lanes_;std::vector<LaneDestination>destinations_;std::optional<std::size_t>selected_;
};
}
