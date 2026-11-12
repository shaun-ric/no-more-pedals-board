#include "Core/Graph.h"
#include <cstdio>
struct M : Node {
  int got=0, offs=0; bool in_=false,out_=false;
  M(std::string id, ParamRegistry& r,bool i=false,bool o=false):Node(id,id,"m",r,true),in_(i),out_(o){}
  bool acceptsMidi() const override {return !in_&&!out_;}
  bool isInput() const override {return in_;} bool isOutput() const override {return out_;}
  int deviceInputChannel() const override {return in_?0:-1;} int deviceOutputChannel() const override {return out_?0:-1;}
  void process(juce::AudioBuffer<float>& b,int n) override { if(midiIn) for(auto m: *midiIn){ got++; if(m.getMessage().isNoteOff()) offs++; } b.clear(); }
};
int main(){
  ParamRegistry reg; Graph g(reg); NoteBus bus; g.setNoteBus(&bus);
  auto m=std::make_shared<M>("syn",reg);
  g.addNode(std::make_shared<M>("in",reg,true)); g.addNode(m); g.addNode(std::make_shared<M>("out",reg,false,true));
  g.connect("syn","out"); g.prepare(48000,64); juce::String e; g.commit(e);
  printf("pushed on=%d cc7=%d pb=%d\n", bus.push(juce::MidiMessage::noteOn(5,60,(juce::uint8)100)), bus.push(juce::MidiMessage::controllerEvent(5,7,10)), bus.push(juce::MidiMessage::pitchWheel(5,9000)));
  bus.push(juce::MidiMessage::noteOff(5,60));
  float in[64]={0},o[64]={0}; const float* ins[1]={in}; float* outs[1]={o};
  g.process(ins,1,outs,1,64);
  printf("events %d offs %d\n", m->got, m->offs);
  g.process(ins,1,outs,1,64);
  printf("events after 2nd block %d\n", m->got);
}
