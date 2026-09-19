#pragma once
// Visual language of the single-screen editor (ARCHITECTURE "State, editing and UI", D6): exactly two tokens
// (off-white ground, off-black ink), one modulation accent for effective-value rings and the router's per-cable palette.
// Uppercase text, hairline strokes, vector glyphs, no bitmaps/gradients and no JUCE default look-and-feel.
#include "iupac/ui/CableRouter.hpp"
#include "iupac/ui/Layout.hpp"
#include <juce_audio_utils/juce_audio_utils.h>
namespace iupac::ui
{
inline const juce::Colour ground{0xff000000|(offWhiteGround.r<<16)|(offWhiteGround.g<<8)|offWhiteGround.b};
inline const juce::Colour ink{0xff1b1a17};
inline const juce::Colour accent{0xfff0a500};
inline constexpr float hairline=1.0f;
[[nodiscard]]inline juce::Colour cableColour(int index)noexcept{const auto&c=cablePalette[(std::size_t)juce::jlimit(0,(int)cablePalette.size()-1,index)];return juce::Colour(c.r,c.g,c.b);}
inline constexpr float minimumTextHeight=(float)minimumFontHeight;
[[nodiscard]]juce::Font labelFont(float height);
// The editor's live text scale, published by IupacSynthEditor::resized() on its own EditorLookAndFeel and read back
// through each child's look-and-feel, so two editors open at different sizes scale independently (#89).
[[nodiscard]]float editorScale(const juce::Component&)noexcept;
// Pixel height for text authored at `referenceText` in the 1000x700 frame, floored at minimumTextHeight.
[[nodiscard]]float scaledText(const juce::Component&,float referenceText)noexcept;
// Uppercase caption drawn in ink at hairline weight.
void drawCaption(juce::Graphics&,juce::StringRef,juce::Rectangle<int>,juce::Justification=juce::Justification::centred,float height=10.0f);
// Vector glyphs for module types (SRC/RES/FILT/SHAPE/MIX/OUT, catalog module ids) and transport actions (new/load/save/preset/reset).
void drawGlyph(juce::Graphics&,std::string_view id,juce::Rectangle<float>,juce::Colour);
class EditorLookAndFeel final:public juce::LookAndFeel_V4
{
public:
 EditorLookAndFeel();
 juce::Font getTextButtonFont(juce::TextButton&,int)override;juce::Font getComboBoxFont(juce::ComboBox&)override;juce::Font getPopupMenuFont()override;juce::Font getLabelFont(juce::Label&)override;
 void drawButtonBackground(juce::Graphics&,juce::Button&,const juce::Colour&,bool,bool)override;void drawButtonText(juce::Graphics&,juce::TextButton&,bool,bool)override;
 void drawComboBox(juce::Graphics&,int,int,bool,int,int,int,int,juce::ComboBox&)override;void positionComboBoxText(juce::ComboBox&,juce::Label&)override;
 void drawPopupMenuBackground(juce::Graphics&,int,int)override;void drawPopupMenuItem(juce::Graphics&,const juce::Rectangle<int>&,bool,bool,bool,bool,bool,const juce::String&,const juce::String&,const juce::Drawable*,const juce::Colour*)override;
 void drawScrollbar(juce::Graphics&,juce::ScrollBar&,int,int,int,int,bool,int,int,bool,bool)override;int getDefaultScrollbarWidth()override{return juce::roundToInt(scaled(6.0f));}
 void fillTextEditorBackground(juce::Graphics&,int,int,juce::TextEditor&)override;void drawTextEditorOutline(juce::Graphics&,int,int,juce::TextEditor&)override;
 void drawLabel(juce::Graphics&,juce::Label&)override;void drawToggleButton(juce::Graphics&,juce::ToggleButton&,bool,bool)override;
 void drawDocumentWindowTitleBar(juce::DocumentWindow&,juce::Graphics&,int,int,int,int,const juce::Image*,bool)override;
 juce::Button*createDocumentWindowButton(int)override;void drawCallOutBoxBackground(juce::CallOutBox&,juce::Graphics&,const juce::Path&,juce::Image&)override;int getCallOutBoxBorderSize(const juce::CallOutBox&)override{return juce::roundToInt(scaled(6.0f));}
 // Live text scale of the editor that owns this look-and-feel; 1.0 at the 1000x700 reference frame (#89).
 void setScale(float s)noexcept{scale_=s;}[[nodiscard]]float scale()const noexcept{return scale_;}
 [[nodiscard]]float scaled(float referenceText)const noexcept{return(float)fontHeight(referenceText,scale_);}
private:
 float scale_{1.0f};
};
}
