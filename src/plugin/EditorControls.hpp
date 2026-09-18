#pragma once
// Control kit of the single-screen editor (ARCHITECTURE "State, editing and UI", D6/D7): one widget class per parameter
// class — knob, fader, forest, segment toggle, envelope curve, LFO preview — plus the shell services they share
// (value bubble on hover/drag, inline precision entry on double-click, `EDIT` table for arrays). No text fields live on
// the default screen; every value is drawn in ink with the effective (modulated) value as an accent overlay.
#include "EditorLookAndFeel.hpp"
#include "iupac/domain/Patch.hpp"
#include <functional>
#include <optional>
#include <vector>
namespace iupac::ui
{
[[nodiscard]]juce::String formatValue(double value,std::string_view unit);
// Services the editor provides to every control; found through findParentComponentOfClass.
class EditorShell
{
public:
 virtual~EditorShell()=default;
 virtual void showValue(juce::Component&anchor,juce::String text)=0;virtual void hideValue()=0;
 // Inline text/numeric entry over `bounds` (in anchor coordinates); Enter commits, Escape cancels, focus never sends MIDI.
 virtual void openEntry(juce::Component&anchor,juce::Rectangle<int>bounds,juce::String initial,std::function<void(juce::String)>commit)=0;
 [[nodiscard]]virtual bool entryOpen()const=0;
};
class ValueControl:public juce::Component
{
public:
 explicit ValueControl(domain::ParameterDescriptor descriptor);
 void setValue(double value,bool notify);[[nodiscard]]double value()const noexcept{return value_;}
 void setEffective(std::optional<float>normalized);[[nodiscard]]std::optional<float>effective()const noexcept{return effective_;}
 void setDescriptor(domain::ParameterDescriptor d){descriptor_=std::move(d);repaint();}[[nodiscard]]const domain::ParameterDescriptor&descriptor()const noexcept{return descriptor_;}
 void setCaption(juce::String c){caption_=std::move(c);repaint();}[[nodiscard]]const juce::String&caption()const noexcept{return caption_;}
 void setStep(double normalizedStep)noexcept{step_=normalizedStep;}[[nodiscard]]double normalized()const noexcept{return descriptor_.normalize(value_);}
 void setHighlighted(bool h){if(highlighted_!=h){highlighted_=h;repaint();}}
 [[nodiscard]]juce::String formatted()const{return formatValue(value_,descriptor_.unit);}
 std::function<void(double)>onChange;std::function<void()>onGestureStart,onGestureEnd;
 void mouseEnter(const juce::MouseEvent&)override;void mouseExit(const juce::MouseEvent&)override;void mouseDown(const juce::MouseEvent&)override;void mouseDrag(const juce::MouseEvent&)override;void mouseUp(const juce::MouseEvent&)override;void mouseDoubleClick(const juce::MouseEvent&)override;void mouseWheelMove(const juce::MouseEvent&,const juce::MouseWheelDetails&)override;
protected:
 [[nodiscard]]virtual double dragDelta(const juce::MouseEvent&)const=0;// normalized change since mouse-down
 void openEntry();void bubble();
 domain::ParameterDescriptor descriptor_;double value_{},dragStart_{},step_{0.01};std::optional<float>effective_;juce::String caption_;bool highlighted_{},dragging_{};
};
class Knob final:public ValueControl
{
public:
 using ValueControl::ValueControl;void paint(juce::Graphics&)override;
protected:
 double dragDelta(const juce::MouseEvent&)const override;
};
class Fader final:public ValueControl
{
public:
 Fader(domain::ParameterDescriptor,bool vertical,bool bipolar);void paint(juce::Graphics&)override;[[nodiscard]]bool vertical()const noexcept{return vertical_;}
protected:
 double dragDelta(const juce::MouseEvent&)const override;
private:
 bool vertical_,bipolar_;
};
// Vertical lines from a shared baseline, one per array element; press-drag paints values across the columns.
class Forest final:public juce::Component
{
public:
 enum class Mode{unipolar,bipolar,logDeviation};
 Forest(domain::ParameterDescriptor,Mode,std::vector<double>defaults={});
 void setValues(std::vector<double>);[[nodiscard]]const std::vector<double>&values()const noexcept{return values_;}
 void setCaption(juce::String c){caption_=std::move(c);repaint();}[[nodiscard]]const domain::ParameterDescriptor&descriptor()const noexcept{return descriptor_;}
 std::function<void(const std::vector<double>&)>onChange;
 void paint(juce::Graphics&)override;void mouseMove(const juce::MouseEvent&)override;void mouseExit(const juce::MouseEvent&)override;void mouseDown(const juce::MouseEvent&)override;void mouseDrag(const juce::MouseEvent&)override;void mouseUp(const juce::MouseEvent&)override;void mouseDoubleClick(const juce::MouseEvent&)override;void mouseWheelMove(const juce::MouseEvent&,const juce::MouseWheelDetails&)override;
 [[nodiscard]]int columnAt(float x)const noexcept;[[nodiscard]]float columnX(int column)const noexcept;
private:
 [[nodiscard]]double yToValue(int column,float y)const noexcept;[[nodiscard]]float valueToY(int column,double value)const noexcept;[[nodiscard]]double baseline(int column)const noexcept;void paintStroke(juce::Point<float>from,juce::Point<float>to);
 domain::ParameterDescriptor descriptor_;Mode mode_;std::vector<double>defaults_,values_;juce::String caption_;int hovered_{-1};std::optional<juce::Point<float>>last_;
};
class SegmentToggle final:public juce::Component
{
public:
 explicit SegmentToggle(std::vector<juce::String>segments);void setIndex(int,bool notify);[[nodiscard]]int index()const noexcept{return index_;}
 std::function<void(int)>onChange;void paint(juce::Graphics&)override;void mouseDown(const juce::MouseEvent&)override;
private:
 std::vector<juce::String>segments_;int index_{};
};
class AdsrCurve final:public juce::Component
{
public:
 void setEnvelope(domain::Envelope,bool notify);[[nodiscard]]domain::Envelope envelope()const noexcept{return envelope_;}void setCaption(juce::String c){caption_=std::move(c);repaint();}
 std::function<void(domain::Envelope)>onChange;
 void paint(juce::Graphics&)override;void mouseMove(const juce::MouseEvent&)override;void mouseExit(const juce::MouseEvent&)override;void mouseDown(const juce::MouseEvent&)override;void mouseDrag(const juce::MouseEvent&)override;void mouseUp(const juce::MouseEvent&)override;void mouseDoubleClick(const juce::MouseEvent&)override;
private:
 [[nodiscard]]std::array<juce::Point<float>,4>handles()const noexcept;[[nodiscard]]int handleAt(juce::Point<float>)const noexcept;void bubble(int handle);
 domain::Envelope envelope_;juce::String caption_;int hovered_{-1},dragged_{-1};
};
class LfoPreview final:public juce::Component
{
public:
 void setLfo(domain::Lfo l){lfo_=l;repaint();}void setCaption(juce::String c){caption_=std::move(c);repaint();}void paint(juce::Graphics&)override;
private:
 domain::Lfo lfo_;juce::String caption_;
};
class GlyphButton final:public juce::Button
{
public:
 explicit GlyphButton(juce::String glyph):juce::Button(glyph),glyph_(std::move(glyph)){}void paintButton(juce::Graphics&,bool,bool)override;
private:
 juce::String glyph_;
};
// Compact numeric table for a whole array stack: one column per array, one row per element (the only table in the editor).
class ArrayTable final:public juce::Component
{
public:
 struct Column{juce::String caption;domain::ParameterDescriptor descriptor;std::vector<double>values;std::function<void(const std::vector<double>&)>commit;};
 explicit ArrayTable(std::vector<Column>);void resized()override;void paint(juce::Graphics&)override;
private:
 void commitCell(std::size_t column,std::size_t row);
 std::vector<Column>columns_;juce::OwnedArray<juce::TextEditor>cells_;
};
class PrecisionEntry final:public juce::TextEditor
{
public:
 PrecisionEntry();std::function<void(juce::String)>onCommit;std::function<void()>onCancel;
 bool keyPressed(const juce::KeyPress&)override;void focusLost(FocusChangeType)override;
};
}
