#include "EditorField.hpp"
#include <cmath>
namespace iupac::ui
{
namespace
{
SlotKind kindOf(domain::ModuleType t){switch(t){case domain::ModuleType::harmonic:case domain::ModuleType::fm:case domain::ModuleType::noise:return SlotKind::source;case domain::ModuleType::resonator:return SlotKind::resonator;case domain::ModuleType::filter:return SlotKind::filter;case domain::ModuleType::shaper:return SlotKind::shaper;case domain::ModuleType::mixer:return SlotKind::mixer;}return SlotKind::source;}
std::string_view idPrefix(SlotKind k){switch(k){case SlotKind::source:return"src";case SlotKind::resonator:return"res";case SlotKind::filter:return"filt";case SlotKind::shaper:return"shape";case SlotKind::mixer:return"mix";case SlotKind::output:return"out";}return"";}
int nodeIndex(const domain::Patch&p,std::string_view id){for(std::size_t i=0;i<p.nodes.size();++i)if(p.nodes[i].id==id)return(int)i;return-1;}
domain::ParameterDescriptor describe(const domain::ModuleDescriptor&m,std::string_view id){const auto*d=domain::findParameter(m,id);return d?*d:domain::ParameterDescriptor{id,"",0,1,0,domain::ParameterScale::linear,domain::ParameterKind::continuous,false,0,20.0,{}};}
constexpr double defaultCableGain=0.8;constexpr float portRadius=4.5f;
juce::Path roundedPolyline(const std::vector<juce::Point<float>>&pts,float radius)
{
 juce::Path path;if(pts.empty())return path;path.startNewSubPath(pts[0]);
 for(std::size_t i=1;i+1<pts.size();++i){const auto in=pts[i]-pts[i-1],out=pts[i+1]-pts[i];const float li=in.getDistanceFromOrigin(),lo=out.getDistanceFromOrigin();if(li<0.01f||lo<0.01f){path.lineTo(pts[i]);continue;}const float r=juce::jmin(radius,li*0.5f,lo*0.5f);const auto a=pts[i]-in*(r/li),b=pts[i]+out*(r/lo);path.lineTo(a);path.quadraticTo(pts[i],b);}
 path.lineTo(pts.back());return path;
}
float segmentDistance(juce::Point<float>p,juce::Point<float>a,juce::Point<float>b){const auto ab=b-a;const float l2=ab.x*ab.x+ab.y*ab.y;const float t=l2<0.001f?0.0f:juce::jlimit(0.0f,1.0f,((p.x-a.x)*ab.x+(p.y-a.y)*ab.y)/l2);return p.getDistanceFrom(a+ab*t);}
}
std::string slotNodeId(std::size_t slot){const auto&s=slotTable[slot];return std::string(idPrefix(s.kind))+std::to_string(s.instance+1);}
std::string uniqueNodeId(const domain::Patch&p,std::size_t slot){auto base=slotNodeId(slot);if(nodeIndex(p,base)<0)return base;for(int i=2;i<1000;++i){auto s=base+"-"+std::to_string(i);if(nodeIndex(p,s)<0)return s;}return{};}
bool slotAccepts(std::size_t slot,domain::ModuleType t){return slot<moduleSlotCount&&slotTable[slot].kind==kindOf(t);}
domain::Node defaultNode(std::string id,const domain::ModuleDescriptor&d){domain::Node n{std::move(id),d.type,{}};for(const auto&p:d.parameters)n.parameters.push_back({std::string(p.id),std::vector<double>(p.arraySize?p.arraySize:1,p.defaultValue)});if(d.type==domain::ModuleType::harmonic){n.parameters[0].values[0]=1;for(std::size_t i=0;i<16;++i)n.parameters[1].values[i]=double(i+1);}return n;}
SlotMap assignSlots(const domain::Patch&p)
{
 SlotMap m;auto place=[&](std::size_t node,std::size_t slot){m.node[slot]=(int)node;m.slot[node]=(int)slot;};
 for(std::size_t i=0;i<p.nodes.size()&&i<domain::maximumNodes;++i)for(std::size_t s=0;s<moduleSlotCount;++s)if(m.node[s]<0&&slotTable[s].kind==kindOf(p.nodes[i].type)&&p.nodes[i].id==slotNodeId(s)){place(i,s);break;}
 for(std::size_t i=0;i<p.nodes.size()&&i<domain::maximumNodes;++i)if(m.slot[i]<0)for(std::size_t s=0;s<moduleSlotCount;++s)if(m.node[s]<0&&slotTable[s].kind==kindOf(p.nodes[i].type)){place(i,s);break;}
 return m;
}
PortView::PortView(ModuleField&f,std::size_t slot,bool input):field_(f),slot_(slot),input_(input){setRepaintsOnMouseActivity(true);setName(juce::String(input?"IN ":"OUT ")+juce::String(slotKindName(slotTable[slot].kind).data())+juce::String(slotTable[slot].instance+1));}
void PortView::paint(juce::Graphics&g)
{
 const auto c=getLocalBounds().toFloat().getCentre();const bool active=slot_==outputSlot||field_.slot(slot_).active();const juce::Colour colour=state_==State::inert?ink.withAlpha(0.2f):state_==State::idle?ink.withAlpha(active?1.0f:0.3f):accent;
 g.setColour(colour);if(input_){g.drawEllipse(c.x-portRadius,c.y-portRadius,2*portRadius,2*portRadius,state_==State::legal?2.0f:hairline);}else g.fillEllipse(c.x-portRadius,c.y-portRadius,2*portRadius,2*portRadius);
 if(state_==State::legal){g.drawEllipse(c.x-portRadius-3.0f,c.y-portRadius-3.0f,2*portRadius+6.0f,2*portRadius+6.0f,hairline);}
}
void PortView::mouseEnter(const juce::MouseEvent&){if(field_.dragKind()==ModuleField::DragKind::none&&state_==State::idle)setState(State::highlighted);}
void PortView::mouseExit(const juce::MouseEvent&){if(field_.dragKind()==ModuleField::DragKind::none&&state_==State::highlighted)setState(State::idle);}
void PortView::mouseDown(const juce::MouseEvent&e)
{
 if(e.mods.isPopupMenu())return;if(!input_){if(field_.slot(slot_).active())field_.beginConnect(slot_);return;}
 const auto destination=field_.destinationId(slot_);const auto&cables=field_.cables().cables();for(std::size_t i=cables.size();i-->0;)if(cables[i].edge.destination==destination){field_.beginDetach(i,true);return;}
}
void PortView::mouseDrag(const juce::MouseEvent&e){field_.updateDrag(e.getEventRelativeTo(&field_).position);}
void PortView::mouseUp(const juce::MouseEvent&e){field_.endDrag(e.getEventRelativeTo(&field_).position);}
SlotView::SlotView(ModuleField&f,std::size_t slot):field_(f),slot_(slot){setRepaintsOnMouseActivity(true);setName(juce::String(slotKindName(slotTable[slot].kind).data())+(slot==outputSlot?juce::String():juce::String(slotTable[slot].instance+1)));}
void SlotView::paint(juce::Graphics&g)
{
 const auto&s=slotTable[slot_];auto r=getLocalBounds().toFloat().reduced(0.5f);const float glyph=juce::jmin(r.getHeight()*0.28f,r.getWidth()*0.3f,26.0f);
 if(slot_==outputSlot){g.setColour(ink);g.drawRoundedRectangle(r,3.0f,hairline);drawGlyph(g,"OUT",juce::Rectangle<float>(glyph,glyph).withCentre(r.getCentre().translated(0,-glyph*0.3f)),ink);drawCaption(g,"OUT",r.toNearestInt().withTop((int)(r.getCentreY()+glyph*0.35f)).withHeight(12),juce::Justification::centred,9.0f);return;}
 if(!active()){const float dash[]{3.0f,3.0f};juce::Path p;p.addRoundedRectangle(r,3.0f);juce::PathStrokeType(hairline).createDashedStroke(p,p,dash,2);g.setColour(ink.withAlpha(0.7f));g.fillPath(p);if(isMouseOver()){g.setColour(ink.withAlpha(0.05f));g.fillRoundedRectangle(r,3.0f);}
  drawGlyph(g,slotKindName(s.kind),juce::Rectangle<float>(glyph,glyph).withCentre(r.getCentre().translated(0,-glyph*0.3f)),ink.withAlpha(0.7f));g.setColour(ink.withAlpha(0.7f));drawCaption(g,juce::String(slotKindName(s.kind).data())+" "+juce::String(s.instance+1),r.toNearestInt().withTop((int)(r.getCentreY()+glyph*0.35f)).withHeight(12),juce::Justification::centred,9.0f);return;}
 g.setColour(ground);g.fillRoundedRectangle(r,3.0f);g.setColour(ink);g.drawRoundedRectangle(r,3.0f,hairline);
 const int header=juce::jlimit(10,14,(int)((float)getHeight()*0.16f));const auto&d=domain::moduleCatalog()[(std::size_t)type_];const float gs=(float)header-4.0f;drawGlyph(g,d.id,juce::Rectangle<float>(4.0f,2.0f,gs,gs),ink);g.setColour(ink);drawCaption(g,juce::String(d.id.data()),juce::Rectangle<int>(header+2,0,getWidth()-header-24,header),juce::Justification::centredLeft,juce::jmin(9.0f,(float)header-3.0f));
 g.setColour(ink.withAlpha(0.35f));g.drawLine(2.0f,(float)header+0.5f,(float)getWidth()-2.0f,(float)header+0.5f,hairline);
}
void SlotView::mouseDown(const juce::MouseEvent&e){pressed_=!active()&&slot_!=outputSlot&&!e.mods.isPopupMenu();}
void SlotView::mouseUp(const juce::MouseEvent&e)
{
 if(!pressed_||!getLocalBounds().contains(e.getPosition())){pressed_=false;return;}pressed_=false;const auto&s=slotTable[slot_];
 if(s.kind!=SlotKind::source){static constexpr std::array types{"","resonator","filter","shaper","mixer",""};if(field_.onActivate)field_.onActivate(types[(std::size_t)s.kind],slot_);return;}
 juce::PopupMenu m;m.addItem(1,"harmonic");m.addItem(2,"fm");m.addItem(3,"noise");juce::Component::SafePointer<SlotView>self(this);
 m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMinimumWidth(80),[self](int r){if(!self||r==0||!self->field_.onActivate)return;self->field_.onActivate(r==1?"harmonic":r==2?"fm":"noise",self->slot_);});
}
void SlotView::setNode(const domain::Node*n)
{
 if(!n){if(active()){nodeId_.clear();controls_.clear();remove_.reset();edit_.reset();}repaint();return;}
 if(nodeId_!=n->id||type_!=n->type||controls_.empty())build(*n);sync(*n);
}
void SlotView::build(const domain::Node&n)
{
 controls_.clear();nodeId_=n.id;type_=n.type;const auto&d=domain::moduleCatalog()[(std::size_t)n.type];
 auto knob=[&](std::string_view id,juce::String caption){auto k=std::make_unique<Knob>(describe(d,id));k->setCaption(std::move(caption));k->setStep(0.02);k->onChange=[this,p=std::string(id)](double v){commit(p,{v});};addAndMakeVisible(*k);controls_.emplace_back(std::string(id),std::move(k));};
 auto fader=[&](std::string_view id,juce::String caption,bool vertical,bool bipolar){auto f=std::make_unique<Fader>(describe(d,id),vertical,bipolar);f->setCaption(std::move(caption));f->setStep(0.02);f->onChange=[this,p=std::string(id)](double v){commit(p,{v});};addAndMakeVisible(*f);controls_.emplace_back(std::string(id),std::move(f));};
 auto forest=[&](std::string_view id,juce::String caption,Forest::Mode mode,std::vector<double>defaults={}){auto f=std::make_unique<Forest>(describe(d,id),mode,std::move(defaults));f->setCaption(std::move(caption));f->onChange=[this,p=std::string(id)](const std::vector<double>&v){commit(p,v);};addAndMakeVisible(*f);controls_.emplace_back(std::string(id),std::move(f));};
 auto toggle=[&](std::string_view id,std::vector<juce::String>segments){auto t=std::make_unique<SegmentToggle>(std::move(segments));t->onChange=[this,p=std::string(id)](int v){commit(p,{(double)v});};addAndMakeVisible(*t);controls_.emplace_back(std::string(id),std::move(t));};
 std::vector<double>harmonicDefaults(16);for(std::size_t i=0;i<16;++i)harmonicDefaults[i]=(double)(i+1);
 switch(n.type)
 {
  case domain::ModuleType::harmonic:forest("partialAmplitudes","amp",Forest::Mode::unipolar);forest("partialRatios","ratio",Forest::Mode::logDeviation,harmonicDefaults);forest("partialPans","pan",Forest::Mode::bipolar);fader("tilt","tilt",false,true);fader("inharmonicity","inharm",false,false);break;
  case domain::ModuleType::fm:knob("carrierRatio","carr");knob("modulatorRatio","mod");knob("index","index");break;
  case domain::ModuleType::noise:toggle("color",{"white","pink"});toggle("mode",{"cont","burst"});knob("burstMs","burst");break;
  case domain::ModuleType::resonator:toggle("mode",{"comb","modal"});knob("tuneRatio","tune");knob("combFeedback","feedback");knob("modalQ","modal q");forest("modeRatios","ratio",Forest::Mode::logDeviation,std::vector<double>(4,1.0));forest("modeLevels","level",Forest::Mode::unipolar);break;
  case domain::ModuleType::filter:toggle("mode",{"lp","bp","hp"});knob("cutoff","cutoff");knob("q","q");break;
  case domain::ModuleType::shaper:knob("drive","drive");fader("wet","wet",false,false);break;
  case domain::ModuleType::mixer:fader("level","level",false,false);fader("pan","pan",false,true);break;
 }
 fader("outputLevel","out",true,false);
 remove_=std::make_unique<GlyphButton>("remove");remove_->setName("Deactivate "+getName());remove_->onClick=[this]{if(field_.onDeactivate)field_.onDeactivate(slot_);};addAndMakeVisible(*remove_);
 if(n.type==domain::ModuleType::harmonic||n.type==domain::ModuleType::resonator){edit_=std::make_unique<GlyphButton>("edit");edit_->setName("Edit "+getName()+" arrays");edit_->onClick=[this]{openTable();};addAndMakeVisible(*edit_);}else edit_.reset();
 resized();
}
void SlotView::sync(const domain::Node&n)
{
 nodeId_=n.id;for(const auto&p:n.parameters){for(auto&[id,c]:controls_)if(id==p.id){if(auto*v=dynamic_cast<ValueControl*>(c.get()))v->setValue(p.values[0],false);else if(auto*f=dynamic_cast<Forest*>(c.get()))f->setValues(p.values);else if(auto*t=dynamic_cast<SegmentToggle*>(c.get())){t->setIndex((int)p.values[0],false);if(id=="mode"&&mode_!=(int)p.values[0]){mode_=(int)p.values[0];resized();}}}}
 repaint();
}
void SlotView::commit(std::string_view p,std::vector<double>v){if(field_.onParameter)field_.onParameter(nodeId_,std::string(p),std::move(v));}
ValueControl*SlotView::control(std::string_view p)const{for(auto&[id,c]:controls_)if(id==p)return dynamic_cast<ValueControl*>(c.get());return nullptr;}
Forest*SlotView::forest(std::string_view p)const{for(auto&[id,c]:controls_)if(id==p)return dynamic_cast<Forest*>(c.get());return nullptr;}
SegmentToggle*SlotView::toggle(std::string_view p)const{for(auto&[id,c]:controls_)if(id==p)return dynamic_cast<SegmentToggle*>(c.get());return nullptr;}
void SlotView::setHighlightedParameter(std::string_view p){for(auto&[id,c]:controls_)if(auto*v=dynamic_cast<ValueControl*>(c.get()))v->setHighlighted(id==p);}
void SlotView::setEffective(const std::array<float,engine::parameterTargetCount>*values,const std::vector<std::string>&targeted)
{
 for(auto&[id,c]:controls_)if(auto*v=dynamic_cast<ValueControl*>(c.get())){std::optional<float>e;if(values)if(auto t=engine::parameterTarget(id))if(std::ranges::find(targeted,id)!=targeted.end()){const float x=(*values)[(std::size_t)*t];if(x>=0.0f)e=x;}v->setEffective(e);}
}
void SlotView::resized()
{
 if(!active())return;const int header=juce::jlimit(10,14,(int)((float)getHeight()*0.16f));auto r=getLocalBounds();auto top=r.removeFromTop(header);remove_->setBounds(top.removeFromRight(header).reduced(1));if(edit_)edit_->setBounds(top.removeFromRight(header).reduced(1));
 auto body=r.reduced(3,2);if(auto*out=control("outputLevel"))out->setBounds(body.removeFromRight(10));body.removeFromRight(2);
 auto place=[&](std::string_view id,juce::Rectangle<int>b){for(auto&[cid,c]:controls_)if(cid==id){c->setBounds(b);c->setVisible(true);}};auto hide=[&](std::string_view id){for(auto&[cid,c]:controls_)if(cid==id)c->setVisible(false);};
 switch(type_)
 {
  case domain::ModuleType::harmonic:{auto faders=body.removeFromBottom(juce::jmin(14,body.getHeight()/4));const int h=body.getHeight()/3;place("partialAmplitudes",body.removeFromTop(h));place("partialRatios",body.removeFromTop(h));place("partialPans",body);place("tilt",faders.removeFromLeft(faders.getWidth()/2).reduced(1,0));place("inharmonicity",faders.reduced(1,0));break;}
  case domain::ModuleType::fm:{const int w=body.getWidth()/3;place("carrierRatio",body.removeFromLeft(w));place("modulatorRatio",body.removeFromLeft(w));place("index",body);break;}
  case domain::ModuleType::noise:{auto left=body.removeFromLeft(body.getWidth()*11/20);const int h=juce::jmin(14,left.getHeight()/2);place("color",left.removeFromTop(h).reduced(0,1));place("mode",left.removeFromTop(h).reduced(0,1));place("burstMs",body);break;}
  case domain::ModuleType::resonator:{place("mode",body.removeFromTop(juce::jmin(13,body.getHeight()/5)).reduced(0,1));if(mode_==0){hide("modalQ");hide("modeRatios");hide("modeLevels");const int w=body.getWidth()/2;place("tuneRatio",body.removeFromLeft(w));place("combFeedback",body);}else{hide("combFeedback");auto knobs=body.removeFromTop(body.getHeight()*2/5);const int w=knobs.getWidth()/2;place("tuneRatio",knobs.removeFromLeft(w));place("modalQ",knobs);const int h=body.getHeight()/2;place("modeRatios",body.removeFromTop(h));place("modeLevels",body);}break;}
  case domain::ModuleType::filter:{place("mode",body.removeFromTop(juce::jmin(13,body.getHeight()/5)).reduced(0,1));const int w=body.getWidth()/2;place("cutoff",body.removeFromLeft(w));place("q",body);break;}
  case domain::ModuleType::shaper:{place("drive",body.removeFromLeft(body.getWidth()/2));place("wet",body.withSizeKeepingCentre(body.getWidth(),juce::jmin(24,body.getHeight())));break;}
  case domain::ModuleType::mixer:{const int h=body.getHeight()/2;place("level",body.removeFromTop(h).withSizeKeepingCentre(body.getWidth(),juce::jmin(22,h)));place("pan",body.withSizeKeepingCentre(body.getWidth(),juce::jmin(22,h)));break;}
 }
}
void SlotView::openTable()
{
 std::vector<ArrayTable::Column>columns;for(auto&[id,c]:controls_)if(auto*f=dynamic_cast<Forest*>(c.get()))columns.push_back({juce::String(id).replace("partial","").replace("mode",""),f->descriptor(),f->values(),[this,p=id](const std::vector<double>&v){commit(p,v);}});
 if(columns.empty()||!edit_)return;auto table=std::make_unique<ArrayTable>(std::move(columns));auto&box=juce::CallOutBox::launchAsynchronously(std::move(table),edit_->getScreenBounds(),nullptr);box.setLookAndFeel(&getLookAndFeel());
}
CableLayer::CableLayer(ModuleField&f):field_(f){setName("Cables");}
bool CableLayer::hitTest(int x,int y){for(const auto&[key,k]:knobs_)if(k->getBounds().contains(x,y))return true;return cableAt({(float)x,(float)y},6.0f)>=0;}
Knob*CableLayer::gainKnob(std::string_view s,std::string_view d)const{const auto i=knobs_.find(EdgeKey{std::string(s),std::string(d)});return i==knobs_.end()?nullptr:i->second.get();}
int CableLayer::cableAt(juce::Point<float>p,float tol)const noexcept{int best=-1;float bestDistance=tol;for(std::size_t i=0;i<cables_.size();++i){const auto&pts=cables_[i].points;for(std::size_t s=0;s+1<pts.size();++s){const float d=segmentDistance(p,pts[s],pts[s+1]);if(d<bestDistance){bestDistance=d;best=(int)i;}}}return best;}
void CableLayer::setCables(std::vector<Drawn>c)
{
 cables_=std::move(c);hovered_=-1;
 // Reconcile the gain knobs against the new cable set instead of rebuilding them: every gain edit re-enters here
 // through the document transaction, and destroying the knob under the mouse ended the gesture after one step (#87).
 std::map<EdgeKey,std::unique_ptr<Knob>>kept;
 for(const auto&d:cables_)
 {
  EdgeKey key{d.edge.source,d.edge.destination};std::unique_ptr<Knob>k;
  if(const auto existing=knobs_.find(key);existing!=knobs_.end()){k=std::move(existing->second);knobs_.erase(existing);}
  else
  {
   k=std::make_unique<Knob>(domain::ParameterDescriptor{"gain","",0,1,d.edge.gain,domain::ParameterScale::linear,domain::ParameterKind::continuous,false,0,20.0,{}});
   k->setStep(0.02);k->setCaption("gain");k->setName("Cable gain "+juce::String(d.edge.source)+" to "+juce::String(d.edge.destination));
   k->onChange=[this,e=key](double v){if(field_.onGain)field_.onGain(e.first,e.second,v);};
   addAndMakeVisible(*k);
  }
  // A knob in mid-gesture owns its own value until mouse-up; the drag already tracks the pointer from its own
  // mouse-down anchor, so echoing the applied patch value back would only add a frame of lag.
  if(!k->dragging())k->setValue(d.edge.gain,false);
  // The router reserves the knob's half extent inside the field on every corridor lane (#88), so the
  // fixed-size box always lands inside the layer; painting below uses the same anchor and diameter.
  const int box=(int)knobDiameterPixels;const auto a=field_.toWindow(d.cable.knobAnchor);k->setBounds(juce::Rectangle<int>(box,box).withCentre(a.toInt()));
  kept.insert_or_assign(std::move(key),std::move(k));
 }
 knobs_=std::move(kept);// whatever is left in the old map belongs to removed cables and is destroyed here
 repaint();
}
void CableLayer::paint(juce::Graphics&g)
{
 for(std::size_t i=0;i<cables_.size();++i){const auto&d=cables_[i];const float w=1.0f+3.0f*(float)d.edge.gain;if((int)i==hovered_){g.setColour(ink);g.strokePath(d.path,juce::PathStrokeType(w+3.0f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));}g.setColour(d.colour);g.strokePath(d.path,juce::PathStrokeType(w,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
  const float diameter=(float)knobDiameterPixels,radius=diameter/2.0f;const auto a=field_.toWindow(d.cable.knobAnchor);g.setColour(ground);g.fillEllipse(a.x-radius,a.y-radius,diameter,diameter);g.setColour(d.colour);g.drawEllipse(a.x-radius,a.y-radius,diameter,diameter,hairline);}
}
void CableLayer::mouseMove(const juce::MouseEvent&e)
{
 const int h=cableAt(e.position,6.0f);if(h==hovered_)return;
 auto ports=[&](int index,PortView::State s){if(index<0||(std::size_t)index>=cables_.size())return;const auto&d=cables_[(std::size_t)index];if(auto*p=field_.outputPort(d.sourceSlot))p->setState(s);if(auto*p=field_.inputPort(d.destinationSlot))p->setState(s);};
 if(field_.dragKind()==ModuleField::DragKind::none){ports(hovered_,PortView::State::idle);ports(h,PortView::State::highlighted);}hovered_=h;repaint();
}
void CableLayer::mouseExit(const juce::MouseEvent&)
{
 if(hovered_<0)return;if(field_.dragKind()==ModuleField::DragKind::none){const auto&d=cables_[(std::size_t)hovered_];if(auto*p=field_.outputPort(d.sourceSlot))p->setState(PortView::State::idle);if(auto*p=field_.inputPort(d.destinationSlot))p->setState(PortView::State::idle);}hovered_=-1;repaint();
}
void CableLayer::mouseDown(const juce::MouseEvent&e)
{
 const int i=cableAt(e.position,6.0f);if(i<0)return;const auto edge=cables_[(std::size_t)i].edge;
 if(e.mods.isPopupMenu()){juce::PopupMenu m;m.addItem(1,"remove");juce::Component::SafePointer<CableLayer>self(this);m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea(juce::Rectangle<int>(e.getScreenX(),e.getScreenY(),1,1)),[self,edge](int r){if(self&&r==1&&self->field_.onDisconnect)self->field_.onDisconnect(edge.source,edge.destination);});return;}
 const auto&pts=cables_[(std::size_t)i].points;const float toOut=e.position.getDistanceFrom(pts.front()),toIn=e.position.getDistanceFrom(pts.back());if(juce::jmin(toOut,toIn)<=18.0f)field_.beginDetach((std::size_t)i,toIn<=toOut);
}
void CableLayer::mouseDrag(const juce::MouseEvent&e){field_.updateDrag(e.position);}
void CableLayer::mouseUp(const juce::MouseEvent&e){field_.endDrag(e.position);}
ModuleField::ModuleField()
{
 setName("Module field");for(std::size_t i=0;i<slotCount;++i){slots_.push_back(std::make_unique<SlotView>(*this,i));addAndMakeVisible(*slots_.back());}
 cables_=std::make_unique<CableLayer>(*this);addAndMakeVisible(*cables_);
 for(std::size_t i=0;i<slotCount;++i){if(slotTable[i].hasInput()){ports_.push_back(std::make_unique<PortView>(*this,i,true));addAndMakeVisible(*ports_.back());}if(slotTable[i].hasOutput()){ports_.push_back(std::make_unique<PortView>(*this,i,false));addAndMakeVisible(*ports_.back());}}
}
ModuleField::~ModuleField()=default;
juce::Point<float>ModuleField::toWindow(Point p)const{const auto s=scaleToWindow(p,(double)getWidth(),(double)getHeight());return{(float)s.x,(float)s.y};}
juce::Rectangle<float>ModuleField::slotBounds(std::size_t i)const{const auto r=scaleToWindow(slotTable[i].frame,(double)getWidth(),(double)getHeight());return{(float)r.x,(float)r.y,(float)r.width,(float)r.height};}
PortView*ModuleField::inputPort(std::size_t slot)const{for(auto&p:ports_)if(p->isInput()&&p->slot()==slot)return p.get();return nullptr;}
PortView*ModuleField::outputPort(std::size_t slot)const{for(auto&p:ports_)if(!p->isInput()&&p->slot()==slot)return p.get();return nullptr;}
std::string ModuleField::destinationId(std::size_t slot)const{if(slot==outputSlot)return"output";const int n=map_.node[slot];return n<0?std::string{}:patch_.nodes[(std::size_t)n].id;}
void ModuleField::resized()
{
 for(std::size_t i=0;i<slotCount;++i)slots_[i]->setBounds(slotBounds(i).toNearestInt());cables_->setBounds(getLocalBounds());
 for(auto&p:ports_){const auto a=toWindow(p->isInput()?slotTable[p->slot()].inputAnchor():slotTable[p->slot()].outputAnchor());p->setBounds(juce::Rectangle<int>(14,14).withCentre(a.toInt()));}
 routeCables();
}
void ModuleField::paint(juce::Graphics&g){g.fillAll(ground);}
void ModuleField::paintOverChildren(juce::Graphics&g)
{
 if(drag_==DragKind::none)return;juce::Point<float>from;if(drag_==DragKind::connect)from=toWindow(slotTable[dragSource_].outputAnchor());else{const auto&c=cables_->cables()[dragCable_];from=drag_==DragKind::detachInput?c.points.front():c.points.back();}
 g.setColour(ink);const float dash[]{4.0f,3.0f};juce::Path p;p.startNewSubPath(from);p.lineTo(dragPoint_);juce::Path dashed;juce::PathStrokeType(hairline).createDashedStroke(dashed,p,dash,2);g.fillPath(dashed);g.fillEllipse(dragPoint_.x-3.0f,dragPoint_.y-3.0f,6.0f,6.0f);
}
void ModuleField::setPatch(const domain::Patch&p)
{
 patch_=p;map_=assignSlots(patch_);for(std::size_t i=0;i<moduleSlotCount;++i)slots_[i]->setNode(map_.node[i]>=0?&patch_.nodes[(std::size_t)map_.node[i]]:nullptr);
 routeCables();for(auto&port:ports_)port->repaint();
}
void ModuleField::routeCables()
{
 std::vector<CableEdge>edges;std::vector<domain::AudioEdge>routed;
 for(const auto&e:patch_.edges){const int s=nodeIndex(patch_,e.source);const int d=e.destination=="output"?(int)outputSlot:nodeIndex(patch_,e.destination);if(s<0||d<0||map_.slot[(std::size_t)s]<0||(e.destination!="output"&&map_.slot[(std::size_t)d]<0))continue;edges.push_back({(std::size_t)map_.slot[(std::size_t)s],e.destination=="output"?outputSlot:(std::size_t)map_.slot[(std::size_t)d],e.gain,e.source,e.destination});routed.push_back(e);}
 const auto cables=iupac::ui::routeCables(edges);std::vector<CableLayer::Drawn>drawn;const float radiusScale=(float)juce::jmin(getWidth()/referenceWidth,getHeight()/referenceHeight);
 for(const auto&c:cables){CableLayer::Drawn d;d.cable=c;d.edge=routed[c.edge];d.sourceSlot=edges[c.edge].source;d.destinationSlot=edges[c.edge].destination;d.colour=cableColour(c.colourIndex);for(const auto&pt:c.points)d.points.push_back(toWindow(pt));d.path=roundedPolyline(d.points,(float)c.cornerRadius*radiusScale);drawn.push_back(std::move(d));}
 cables_->setCables(std::move(drawn));
}
void ModuleField::setEffective(const engine::EffectiveValues&v)
{
 for(std::size_t i=0;i<moduleSlotCount;++i){auto&s=*slots_[i];if(!s.active()){s.setEffective(nullptr,{});continue;}std::vector<std::string>targeted;for(const auto&r:patch_.matrix)if(r.enabled&&r.destinationNode==s.nodeId())targeted.push_back(r.destinationParameter);
  const std::uint32_t hash=engine::hashNodeId(s.nodeId());const std::array<float,engine::parameterTargetCount>*values=nullptr;for(std::size_t n=0;n<v.nodeCount&&n<domain::maximumNodes;++n)if(v.nodeIds[n]==hash){values=&v.values[n];break;}s.setEffective(values,targeted);}
}
void ModuleField::setHighlightedParameter(std::string_view nodeId,std::string_view p){for(std::size_t i=0;i<moduleSlotCount;++i)slots_[i]->setHighlightedParameter(slots_[i]->nodeId()==nodeId?p:std::string_view{});}
bool ModuleField::edgeLegal(std::string_view s,std::string_view d)const{if(s.empty()||d.empty())return false;auto p=patch_;p.edges.push_back({std::string(s),std::string(d),defaultCableGain});return domain::validate(p).empty();}
PortView*ModuleField::portAt(juce::Point<float>p)const{for(auto&port:ports_)if(port->getBounds().toFloat().expanded(4.0f).contains(p))return port.get();return nullptr;}
void ModuleField::beginConnect(std::size_t source)
{
 drag_=DragKind::connect;dragSource_=source;dragTarget_=nullptr;legal_.fill(false);const auto src=destinationId(source);
 for(auto&port:ports_){if(!port->isInput())continue;legal_[port->slot()]=edgeLegal(src,destinationId(port->slot()));port->setState(legal_[port->slot()]?PortView::State::legal:PortView::State::inert);}
 if(auto*o=outputPort(source))o->setState(PortView::State::highlighted);dragPoint_=toWindow(slotTable[source].outputAnchor());repaint();
}
void ModuleField::beginDetach(std::size_t cable,bool inputEnd)
{
 if(cable>=cables_->cables().size())return;drag_=inputEnd?DragKind::detachInput:DragKind::detachOutput;dragCable_=cable;dragTarget_=nullptr;legal_.fill(false);const auto&d=cables_->cables()[cable];
 if(inputEnd){auto p=patch_;std::erase_if(p.edges,[&](const auto&e){return e.source==d.edge.source&&e.destination==d.edge.destination;});for(auto&port:ports_){if(!port->isInput())continue;auto trial=p;trial.edges.push_back({d.edge.source,destinationId(port->slot()),d.edge.gain});legal_[port->slot()]=!destinationId(port->slot()).empty()&&domain::validate(trial).empty();port->setState(legal_[port->slot()]?PortView::State::legal:PortView::State::inert);}}
 dragPoint_=inputEnd?d.points.back():d.points.front();repaint();
}
void ModuleField::updateDrag(juce::Point<float>p)
{
 if(drag_==DragKind::none)return;dragPoint_=p;auto*target=portAt(p);if(target&&(!target->isInput()||!legal_[target->slot()]))target=nullptr;
 if(target!=dragTarget_){if(dragTarget_)dragTarget_->setState(PortView::State::legal);dragTarget_=target;if(dragTarget_)dragTarget_->setState(PortView::State::highlighted);}repaint();
}
void ModuleField::endDrag(juce::Point<float>p)
{
 if(drag_==DragKind::none)return;const auto kind=drag_;drag_=DragKind::none;auto*target=portAt(p);const bool legal=target&&target->isInput()&&legal_[target->slot()];
 for(auto&port:ports_)port->setState(PortView::State::idle);repaint();
 if(kind==DragKind::connect){if(legal&&onConnect)onConnect(destinationId(dragSource_),destinationId(target->slot()),defaultCableGain);return;}
 if(dragCable_>=cables_->cables().size())return;const auto edge=cables_->cables()[dragCable_].edge;
 if(target){if(kind==DragKind::detachInput&&legal&&destinationId(target->slot())!=edge.destination){if(onDisconnect)onDisconnect(edge.source,edge.destination);if(onConnect)onConnect(edge.source,destinationId(target->slot()),edge.gain);}return;}
 if(onDisconnect)onDisconnect(edge.source,edge.destination);
}
}
