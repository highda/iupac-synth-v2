#include "EditorControls.hpp"
#include <cmath>
namespace iupac::ui
{
juce::String formatValue(double v,std::string_view unit)
{
 const double a=std::abs(v);juce::String s=a>=100?juce::String(v,0):a>=10?juce::String(v,1):a>=1?juce::String(v,2):juce::String(v,3);
 if(s.containsChar('.'))s=s.trimCharactersAtEnd("0").trimCharactersAtEnd(".");if(s=="-0")s="0";return unit.empty()?s:s+" "+juce::String(std::string(unit)).toUpperCase();
}
namespace
{
EditorShell*shellOf(juce::Component&c){return c.findParentComponentOfClass<EditorShell>();}
juce::String formatEntry(double v){auto s=juce::String(v,4);if(s.containsChar('.'))s=s.trimCharactersAtEnd("0").trimCharactersAtEnd(".");return s;}
std::optional<double>parseNumber(const juce::String&text){const auto t=text.trim();if(t.isEmpty())return std::nullopt;juce::String digits;for(auto c:t)if(juce::CharacterFunctions::isDigit(c)||c=='.'||c=='-'||c=='+'||c=='e'||c=='E')digits<<c;else break;const double v=digits.getDoubleValue();return std::isfinite(v)&&digits.isNotEmpty()?std::optional<double>(v):std::nullopt;}
const domain::ParameterDescriptor attackDescriptor{"attack","s",.001,2,.01,domain::ParameterScale::logarithmic,domain::ParameterKind::continuous,false,0,20.0,{}},decayDescriptor{"decay","s",.01,4,.1,domain::ParameterScale::logarithmic,domain::ParameterKind::continuous,false,0,20.0,{}},sustainDescriptor{"sustain","",0,1,1,domain::ParameterScale::linear,domain::ParameterKind::continuous,false,0,20.0,{}},releaseDescriptor{"release","s",.02,6,.2,domain::ParameterScale::logarithmic,domain::ParameterKind::continuous,false,0,20.0,{}};
}
ValueControl::ValueControl(domain::ParameterDescriptor d):descriptor_(std::move(d)),value_(descriptor_.defaultValue){setRepaintsOnMouseActivity(true);}
void ValueControl::setValue(double v,bool notify){v=juce::jlimit(descriptor_.minimum,descriptor_.maximum,v);if(descriptor_.kind==domain::ParameterKind::discrete)v=std::round(v);const bool changed=!juce::approximatelyEqual(v,value_);value_=v;repaint();if(notify&&changed&&onChange)onChange(value_);}
void ValueControl::setEffective(std::optional<float>e){if(e!=effective_){effective_=e;repaint();}}
void ValueControl::bubble(){if(auto*s=shellOf(*this))s->showValue(*this,caption_.isNotEmpty()?caption_.toUpperCase()+"  "+formatted():formatted());}
void ValueControl::mouseEnter(const juce::MouseEvent&){bubble();}
void ValueControl::mouseExit(const juce::MouseEvent&){if(!dragging_)if(auto*s=shellOf(*this))s->hideValue();}
void ValueControl::mouseDown(const juce::MouseEvent&e){if(e.mods.isPopupMenu())return;dragStart_=normalized();dragging_=true;if(onGestureStart)onGestureStart();bubble();}
void ValueControl::mouseDrag(const juce::MouseEvent&e){if(!dragging_)return;setValue(descriptor_.denormalize(juce::jlimit(0.0,1.0,dragStart_+dragDelta(e))),true);bubble();}
void ValueControl::mouseUp(const juce::MouseEvent&){if(!dragging_)return;dragging_=false;if(onGestureEnd)onGestureEnd();if(!isMouseOver())if(auto*s=shellOf(*this))s->hideValue();}
void ValueControl::mouseDoubleClick(const juce::MouseEvent&){openEntry();}
void ValueControl::mouseWheelMove(const juce::MouseEvent&,const juce::MouseWheelDetails&w){if(juce::approximatelyEqual(w.deltaY,0.0f))return;const double n=juce::jlimit(0.0,1.0,normalized()+(w.deltaY>0?step_:-step_));setValue(descriptor_.denormalize(n),true);bubble();}
void ValueControl::openEntry(){if(auto*s=shellOf(*this)){juce::Component::SafePointer<ValueControl>self(this);s->openEntry(*this,getLocalBounds(),formatEntry(value_),[self](juce::String text){if(!self)return;if(auto v=parseNumber(text))self->setValue(*v,true);});}}
double Knob::dragDelta(const juce::MouseEvent&e)const{return-e.getDistanceFromDragStartY()/(e.mods.isShiftDown()?1200.0:220.0);}
void Knob::paint(juce::Graphics&g)
{
 auto area=getLocalBounds().toFloat();const bool captioned=caption_.isNotEmpty()&&area.getHeight()>=22;juce::Rectangle<float>captionArea;if(captioned)captionArea=area.removeFromBottom(juce::jmin(10.0f,area.getHeight()*0.3f));
 const float d=juce::jmin(area.getWidth(),area.getHeight());auto circle=juce::Rectangle<float>(d,d).withCentre(area.getCentre());const float r=d*0.5f-1.5f;const auto c=circle.getCentre();constexpr float start=-2.356f,span=4.712f;
 juce::Path track;track.addCentredArc(c.x,c.y,r,r,0,start,start+span,true);g.setColour(ink.withAlpha(0.25f));g.strokePath(track,juce::PathStrokeType(hairline));
 const float n=(float)normalized();juce::Path base;base.addCentredArc(c.x,c.y,r,r,0,start,start+span*n,true);g.setColour(ink);g.strokePath(base,juce::PathStrokeType(2.0f));
 g.drawLine(c.x,c.y,c.x+std::sin(start+span*n)*r,c.y-std::cos(start+span*n)*r,hairline);
 if(effective_){juce::Path ring;const float lo=juce::jmin(n,*effective_),hi=juce::jmax(n,*effective_);ring.addCentredArc(c.x,c.y,r-3.0f,r-3.0f,0,start+span*lo,start+span*hi,true);g.setColour(accent);g.strokePath(ring,juce::PathStrokeType(2.0f));g.fillEllipse(c.x+std::sin(start+span**effective_)*(r-3.0f)-1.5f,c.y-std::cos(start+span**effective_)*(r-3.0f)-1.5f,3.0f,3.0f);}
 if(highlighted_){g.setColour(accent);g.drawEllipse(circle.reduced(0.5f),hairline);}
 if(captioned){g.setColour(ink);drawCaption(g,caption_,captionArea.toNearestInt(),juce::Justification::centred,juce::jmin(9.0f,captionArea.getHeight()));}
}
Fader::Fader(domain::ParameterDescriptor d,bool vertical,bool bipolar):ValueControl(std::move(d)),vertical_(vertical),bipolar_(bipolar){}
double Fader::dragDelta(const juce::MouseEvent&e)const{const double fine=e.mods.isShiftDown()?0.2:1.0;const double delta=vertical_?-e.getDistanceFromDragStartY()/juce::jmax(8.0,(double)getHeight()-4):e.getDistanceFromDragStartX()/juce::jmax(8.0,(double)getWidth()-4);double n=dragStart_+delta*fine;if(bipolar_&&std::abs(n-0.5)<0.025)n=0.5;return n-dragStart_;}
void Fader::paint(juce::Graphics&g)
{
 auto area=getLocalBounds().toFloat();const bool captioned=caption_.isNotEmpty()&&!vertical_&&area.getHeight()>=18;juce::Rectangle<float>captionArea;if(captioned)captionArea=area.removeFromBottom(8.0f);
 const float n=(float)normalized(),origin=bipolar_?0.5f:0.0f;g.setColour(ink.withAlpha(0.25f));
 if(vertical_){const float x=area.getCentreX(),top=area.getY()+2,bottom=area.getBottom()-2,h=bottom-top;g.drawLine(x,top,x,bottom,hairline);auto y=[&](float v){return bottom-v*h;};g.setColour(ink);g.drawLine(x,y(origin),x,y(n),3.0f);g.drawLine(x-3.5f,y(n),x+3.5f,y(n),hairline);if(effective_){g.setColour(accent);g.drawLine(x+3.0f,y(n),x+3.0f,y(*effective_),2.0f);g.fillEllipse(x+1.5f,y(*effective_)-1.5f,3.0f,3.0f);}if(bipolar_){g.setColour(ink);g.drawLine(x-2.0f,y(0.5f),x+2.0f,y(0.5f),hairline);}}
 else{const float y=area.getCentreY(),left=area.getX()+2,right=area.getRight()-2,w=right-left;g.drawLine(left,y,right,y,hairline);auto x=[&](float v){return left+v*w;};g.setColour(ink);g.drawLine(x(origin),y,x(n),y,3.0f);g.drawLine(x(n),y-3.5f,x(n),y+3.5f,hairline);if(effective_){g.setColour(accent);g.drawLine(x(n),y+3.0f,x(*effective_),y+3.0f,2.0f);g.fillEllipse(x(*effective_)-1.5f,y+1.5f,3.0f,3.0f);}if(bipolar_){g.setColour(ink);g.drawLine(x(0.5f),y-2.0f,x(0.5f),y+2.0f,hairline);}}
 if(highlighted_){g.setColour(accent);g.drawRect(getLocalBounds(),(int)hairline);}
 if(captioned){g.setColour(ink);drawCaption(g,caption_,captionArea.toNearestInt(),juce::Justification::centredLeft,8.0f);}
}
Forest::Forest(domain::ParameterDescriptor d,Mode m,std::vector<double>defaults):descriptor_(std::move(d)),mode_(m),defaults_(std::move(defaults)),values_(descriptor_.arraySize,descriptor_.defaultValue){if(defaults_.size()!=values_.size())defaults_.assign(values_.size(),descriptor_.defaultValue);setRepaintsOnMouseActivity(true);}
void Forest::setValues(std::vector<double>v){if(v.size()==values_.size()){values_=std::move(v);repaint();}}
int Forest::columnAt(float x)const noexcept{const int n=(int)values_.size();return juce::jlimit(0,n-1,(int)std::floor(x/juce::jmax(1.0f,(float)getWidth())*(float)n));}
float Forest::columnX(int c)const noexcept{return((float)c+0.5f)*(float)getWidth()/(float)values_.size();}
double Forest::baseline(int c)const noexcept{switch(mode_){case Mode::unipolar:return 0.0;case Mode::bipolar:return descriptor_.normalize(0.0);case Mode::logDeviation:(void)c;return 0.5;}return 0.0;}
float Forest::valueToY(int c,double v)const noexcept
{
 double n;if(mode_==Mode::logDeviation){const double dev=std::log2(juce::jlimit(descriptor_.minimum,descriptor_.maximum,v)/defaults_[(std::size_t)c]);n=juce::jlimit(0.0,1.0,0.5+dev*0.5);}else n=descriptor_.normalize(v);
 return(float)getHeight()-2.0f-(float)n*((float)getHeight()-4.0f);
}
double Forest::yToValue(int c,float y)const noexcept
{
 const double n=juce::jlimit(0.0,1.0,((double)getHeight()-2.0-(double)y)/juce::jmax(1.0,(double)getHeight()-4.0));
 if(mode_==Mode::logDeviation)return juce::jlimit(descriptor_.minimum,descriptor_.maximum,defaults_[(std::size_t)c]*std::pow(2.0,(n-0.5)*2.0));
 return descriptor_.denormalize(n);
}
void Forest::paint(juce::Graphics&g)
{
 const int n=(int)values_.size();g.setColour(ink.withAlpha(0.35f));const float by=valueToY(0,mode_==Mode::unipolar?descriptor_.minimum:mode_==Mode::bipolar?0.0:defaults_[0]);g.drawLine(0,by,(float)getWidth(),by,hairline);
 for(int i=0;i<n;++i){const float x=columnX(i),y=valueToY(i,values_[(std::size_t)i]),b=mode_==Mode::logDeviation?valueToY(i,defaults_[(std::size_t)i]):by;g.setColour(i==hovered_?accent:ink);g.drawLine(x,b,x,y,i==hovered_?2.0f:hairline);g.fillEllipse(x-1.0f,y-1.0f,2.0f,2.0f);}
 if(caption_.isNotEmpty()){g.setColour(ink.withAlpha(0.6f));drawCaption(g,caption_,getLocalBounds().removeFromTop(8).withTrimmedLeft(2),juce::Justification::topLeft,7.0f);}
}
void Forest::mouseMove(const juce::MouseEvent&e){hovered_=columnAt(e.position.x);repaint();if(auto*s=shellOf(*this))s->showValue(*this,caption_.toUpperCase()+" "+juce::String(hovered_+1)+"  "+formatValue(values_[(std::size_t)hovered_],descriptor_.unit));}
void Forest::mouseExit(const juce::MouseEvent&){if(!last_){hovered_=-1;repaint();if(auto*s=shellOf(*this))s->hideValue();}}
void Forest::paintStroke(juce::Point<float>from,juce::Point<float>to)
{
 const int a=columnAt(juce::jmin(from.x,to.x)),b=columnAt(juce::jmax(from.x,to.x));bool changed=false;
 for(int c=a;c<=b;++c){const float x=columnX(c);float y;if(std::abs(to.x-from.x)<0.5f)y=to.y;else{const float t=juce::jlimit(0.0f,1.0f,(x-from.x)/(to.x-from.x));y=from.y+t*(to.y-from.y);}
  const double v=yToValue(c,y);if(!juce::approximatelyEqual(v,values_[(std::size_t)c])){values_[(std::size_t)c]=v;changed=true;}}
 hovered_=columnAt(to.x);repaint();if(changed&&onChange)onChange(values_);
}
void Forest::mouseDown(const juce::MouseEvent&e){if(e.mods.isPopupMenu())return;last_=e.position;paintStroke(e.position,e.position);mouseMove(e);}
void Forest::mouseDrag(const juce::MouseEvent&e){if(!last_)return;paintStroke(*last_,e.position);last_=e.position;if(auto*s=shellOf(*this))s->showValue(*this,caption_.toUpperCase()+" "+juce::String(hovered_+1)+"  "+formatValue(values_[(std::size_t)hovered_],descriptor_.unit));}
void Forest::mouseUp(const juce::MouseEvent&e){last_.reset();if(!isMouseOver())mouseExit(e);}
void Forest::mouseDoubleClick(const juce::MouseEvent&e)
{
 const int c=columnAt(e.position.x);if(auto*s=shellOf(*this)){juce::Component::SafePointer<Forest>self(this);const float x=columnX(c);s->openEntry(*this,juce::Rectangle<int>((int)x-24,juce::jmax(0,getHeight()/2-8),48,16),formatEntry(values_[(std::size_t)c]),[self,c](juce::String t){if(!self)return;if(auto v=parseNumber(t)){self->values_[(std::size_t)c]=juce::jlimit(self->descriptor_.minimum,self->descriptor_.maximum,*v);self->repaint();if(self->onChange)self->onChange(self->values_);}});}
}
void Forest::mouseWheelMove(const juce::MouseEvent&e,const juce::MouseWheelDetails&w)
{
 if(juce::approximatelyEqual(w.deltaY,0.0f))return;const int c=columnAt(e.position.x);auto&v=values_[(std::size_t)c];
 if(mode_==Mode::logDeviation)v=juce::jlimit(descriptor_.minimum,descriptor_.maximum,v*std::pow(2.0,(w.deltaY>0?1.0:-1.0)/120.0));else v=descriptor_.denormalize(juce::jlimit(0.0,1.0,descriptor_.normalize(v)+(w.deltaY>0?0.01:-0.01)));
 hovered_=c;repaint();if(onChange)onChange(values_);mouseMove(e);
}
SegmentToggle::SegmentToggle(std::vector<juce::String>s):segments_(std::move(s)){setRepaintsOnMouseActivity(true);}
void SegmentToggle::setIndex(int i,bool notify){i=juce::jlimit(0,(int)segments_.size()-1,i);const bool changed=i!=index_;index_=i;repaint();if(notify&&changed&&onChange)onChange(index_);}
void SegmentToggle::paint(juce::Graphics&g)
{
 auto r=getLocalBounds().toFloat().reduced(0.5f);const float w=r.getWidth()/(float)segments_.size();g.setColour(ink);g.drawRoundedRectangle(r,2.0f,hairline);
 for(std::size_t i=0;i<segments_.size();++i){auto seg=juce::Rectangle<float>(r.getX()+w*(float)i,r.getY(),w,r.getHeight());if((int)i==index_){g.setColour(ink);g.fillRoundedRectangle(seg.reduced(1.0f),1.5f);}if(i)g.drawLine(seg.getX(),seg.getY(),seg.getX(),seg.getBottom(),hairline);g.setColour((int)i==index_?ground:ink);drawCaption(g,segments_[i],seg.toNearestInt(),juce::Justification::centred,juce::jlimit(7.0f,9.0f,r.getHeight()*0.6f));}
}
void SegmentToggle::mouseDown(const juce::MouseEvent&e){if(e.mods.isPopupMenu())return;setIndex((int)std::floor(e.position.x/juce::jmax(1.0f,(float)getWidth())*(float)segments_.size()),true);}
void AdsrCurve::setEnvelope(domain::Envelope env,bool notify){envelope_=env;repaint();if(notify&&onChange)onChange(envelope_);}
std::array<juce::Point<float>,4>AdsrCurve::handles()const noexcept
{
 const float w=(float)getWidth()-6.0f,top=10.0f,bottom=(float)getHeight()-3.0f,x0=3.0f;const float a=x0+w*0.28f*(float)attackDescriptor.normalize(envelope_.attack),d=a+w*0.28f*(float)decayDescriptor.normalize(envelope_.decay),s=d+w*0.12f,r=s+w*0.28f*(float)releaseDescriptor.normalize(envelope_.release);const float sy=bottom-(bottom-top)*(float)envelope_.sustain;
 return{juce::Point<float>(a,top),juce::Point<float>(d,sy),juce::Point<float>(s,sy),juce::Point<float>(r,bottom)};
}
int AdsrCurve::handleAt(juce::Point<float>p)const noexcept{const auto h=handles();int best=-1;float dist=64.0f;for(int i=0;i<4;++i){const float d=h[(std::size_t)i].getDistanceSquaredFrom(p);if(d<dist){dist=d;best=i;}}return best;}
void AdsrCurve::paint(juce::Graphics&g)
{
 const auto h=handles();const float bottom=(float)getHeight()-3.0f;juce::Path p;p.startNewSubPath(3.0f,bottom);p.lineTo(h[0]);p.lineTo(h[1]);p.lineTo(h[2]);p.lineTo(h[3]);g.setColour(ink.withAlpha(0.35f));g.drawLine(3.0f,bottom,(float)getWidth()-3.0f,bottom,hairline);g.setColour(ink);g.strokePath(p,juce::PathStrokeType(hairline));
 for(int i=0;i<4;++i){g.setColour(i==hovered_||i==dragged_?accent:ink);g.fillEllipse(h[(std::size_t)i].x-2.0f,h[(std::size_t)i].y-2.0f,4.0f,4.0f);}
 if(caption_.isNotEmpty()){g.setColour(ink.withAlpha(0.6f));drawCaption(g,caption_,getLocalBounds().removeFromTop(8).withTrimmedLeft(2),juce::Justification::topLeft,7.0f);}
}
void AdsrCurve::bubble(int i){if(auto*s=shellOf(*this)){static constexpr std::array names{"ATTACK","DECAY","SUSTAIN","RELEASE"};const double v=i==0?envelope_.attack:i==1?envelope_.decay:i==2?envelope_.sustain:envelope_.release;s->showValue(*this,caption_.toUpperCase()+" "+names[(std::size_t)juce::jlimit(0,3,i)]+"  "+formatValue(v,i==2?"":"s"));}}
void AdsrCurve::mouseMove(const juce::MouseEvent&e){hovered_=handleAt(e.position);repaint();if(hovered_>=0)bubble(hovered_);else if(auto*s=shellOf(*this))s->hideValue();}
void AdsrCurve::mouseExit(const juce::MouseEvent&){if(dragged_<0){hovered_=-1;repaint();if(auto*s=shellOf(*this))s->hideValue();}}
void AdsrCurve::mouseDown(const juce::MouseEvent&e){if(e.mods.isPopupMenu())return;dragged_=handleAt(e.position);repaint();}
void AdsrCurve::mouseDrag(const juce::MouseEvent&e)
{
 if(dragged_<0)return;const auto h=handles();const float w=(float)getWidth()-6.0f,top=10.0f,bottom=(float)getHeight()-3.0f;auto env=envelope_;
 auto timeFrom=[&](const domain::ParameterDescriptor&d,float x,float origin){return d.denormalize(juce::jlimit(0.0,1.0,(double)(x-origin)/(double)(w*0.28f)));};
 switch(dragged_){case 0:env.attack=timeFrom(attackDescriptor,e.position.x,3.0f);break;case 1:env.decay=timeFrom(decayDescriptor,e.position.x,h[0].x);env.sustain=juce::jlimit(0.0,1.0,(double)(bottom-e.position.y)/(double)(bottom-top));break;case 2:env.sustain=juce::jlimit(0.0,1.0,(double)(bottom-e.position.y)/(double)(bottom-top));break;default:env.release=timeFrom(releaseDescriptor,e.position.x,h[2].x);}
 setEnvelope(env,true);bubble(dragged_);
}
void AdsrCurve::mouseUp(const juce::MouseEvent&e){dragged_=-1;repaint();if(!isMouseOver())mouseExit(e);}
void AdsrCurve::mouseDoubleClick(const juce::MouseEvent&e)
{
 const int i=handleAt(e.position);if(i<0)return;if(auto*s=shellOf(*this)){juce::Component::SafePointer<AdsrCurve>self(this);const auto h=handles()[(std::size_t)i];const double v=i==0?envelope_.attack:i==1?envelope_.decay:i==2?envelope_.sustain:envelope_.release;
  s->openEntry(*this,juce::Rectangle<int>((int)h.x-24,(int)h.y-8,48,16),formatEntry(v),[self,i](juce::String t){if(!self)return;auto parsed=parseNumber(t);if(!parsed)return;auto env=self->envelope_;switch(i){case 0:env.attack=juce::jlimit(attackDescriptor.minimum,attackDescriptor.maximum,*parsed);break;case 1:env.decay=juce::jlimit(decayDescriptor.minimum,decayDescriptor.maximum,*parsed);break;case 2:env.sustain=juce::jlimit(sustainDescriptor.minimum,sustainDescriptor.maximum,*parsed);break;default:env.release=juce::jlimit(releaseDescriptor.minimum,releaseDescriptor.maximum,*parsed);}self->setEnvelope(env,true);});}
}
void LfoPreview::paint(juce::Graphics&g)
{
 auto r=getLocalBounds().toFloat().reduced(3.0f,3.0f);r.removeFromTop(7.0f);juce::Path p;const int n=48;const float cycles=2.0f;
 for(int i=0;i<=n;++i){const float t=(float)i/(float)n*cycles;float v=lfo_.waveform==domain::LfoWaveform::sine?std::sin(t*juce::MathConstants<float>::twoPi):(4.0f*std::abs(t-std::floor(t+0.5f))-1.0f);const auto pt=juce::Point<float>(r.getX()+(float)i/(float)n*r.getWidth(),r.getCentreY()-v*r.getHeight()*0.45f);if(i)p.lineTo(pt);else p.startNewSubPath(pt);}
 g.setColour(ink.withAlpha(0.35f));g.drawLine(r.getX(),r.getCentreY(),r.getRight(),r.getCentreY(),hairline);g.setColour(ink);g.strokePath(p,juce::PathStrokeType(hairline));
 if(caption_.isNotEmpty()){g.setColour(ink.withAlpha(0.6f));drawCaption(g,caption_+"  "+formatValue(lfo_.rate,"Hz"),getLocalBounds().removeFromTop(8).withTrimmedLeft(2),juce::Justification::topLeft,7.0f);}
}
void GlyphButton::paintButton(juce::Graphics&g,bool over,bool down){auto r=getLocalBounds().toFloat();const bool on=down||getToggleState();if(on){g.setColour(ink);g.fillRoundedRectangle(r.reduced(0.5f),2.0f);}else if(over){g.setColour(ink.withAlpha(0.1f));g.fillRoundedRectangle(r.reduced(0.5f),2.0f);}drawGlyph(g,glyph_.toStdString(),r.reduced(juce::jmax(2.0f,r.getWidth()*0.25f)),on?ground:ink);}
ArrayTable::ArrayTable(std::vector<Column>c):columns_(std::move(c))
{
 const std::size_t rows=columns_.empty()?0:columns_[0].values.size();
 for(std::size_t col=0;col<columns_.size();++col)for(std::size_t row=0;row<rows;++row){auto*e=cells_.add(new juce::TextEditor);e->setText(formatEntry(columns_[col].values[row]),false);e->setJustification(juce::Justification::centredRight);e->setFont(labelFont(10.0f));e->setSelectAllWhenFocused(true);e->onReturnKey=[this,col,row]{commitCell(col,row);};e->onFocusLost=[this,col,row]{commitCell(col,row);};addAndMakeVisible(e);}
 setSize(juce::jmax(80,40+(int)columns_.size()*64),18+(int)rows*18);
}
void ArrayTable::resized(){const std::size_t rows=columns_.empty()?0:columns_[0].values.size();for(std::size_t col=0;col<columns_.size();++col)for(std::size_t row=0;row<rows;++row)cells_[(int)(col*rows+row)]->setBounds(40+(int)col*64,18+(int)row*18,62,17);}
void ArrayTable::paint(juce::Graphics&g){g.fillAll(ground);g.setColour(ink);const std::size_t rows=columns_.empty()?0:columns_[0].values.size();for(std::size_t col=0;col<columns_.size();++col)drawCaption(g,columns_[col].caption,juce::Rectangle<int>(40+(int)col*64,0,62,18),juce::Justification::centred,8.0f);for(std::size_t row=0;row<rows;++row)drawCaption(g,juce::String((int)row+1),juce::Rectangle<int>(0,18+(int)row*18,36,17),juce::Justification::centredRight,8.0f);}
void ArrayTable::commitCell(std::size_t col,std::size_t row)
{
 auto&column=columns_[col];auto*cell=cells_[(int)(col*column.values.size()+row)];const auto v=parseNumber(cell->getText());const double clamped=v?juce::jlimit(column.descriptor.minimum,column.descriptor.maximum,*v):column.values[row];cell->setText(formatEntry(clamped),false);
 if(!juce::approximatelyEqual(clamped,column.values[row])){column.values[row]=clamped;if(column.commit)column.commit(column.values);}
}
PrecisionEntry::PrecisionEntry(){setJustification(juce::Justification::centred);setFont(labelFont(10.0f));setSelectAllWhenFocused(true);setIndents(2,1);}
bool PrecisionEntry::keyPressed(const juce::KeyPress&k){if(k==juce::KeyPress::returnKey){if(onCommit)onCommit(getText());return true;}if(k==juce::KeyPress::escapeKey){if(onCancel)onCancel();return true;}return juce::TextEditor::keyPressed(k);}
void PrecisionEntry::focusLost(FocusChangeType t){juce::TextEditor::focusLost(t);if(onCancel)onCancel();}
}
