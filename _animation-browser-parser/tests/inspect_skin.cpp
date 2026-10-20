// Read-only ufbx skin-influence measurement; never exports or alters a source.
#include <ufbx.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <vector>
using J=nlohmann::json;
static J histogram(const std::map<size_t,size_t>& counts){J result=J::object();for(auto [n,count]:counts)result[std::to_string(n)]=count;return result;}
static int inspect(const std::filesystem::path& source,const std::filesystem::path& output){
 ufbx_load_opts opts{};opts.target_axes=ufbx_axes_right_handed_y_up;opts.target_unit_meters=1;opts.space_conversion=UFBX_SPACE_CONVERSION_TRANSFORM_ROOT;opts.geometry_transform_handling=UFBX_GEOMETRY_TRANSFORM_HANDLING_HELPER_NODES;opts.inherit_mode_handling=UFBX_INHERIT_MODE_HANDLING_HELPER_NODES;opts.load_external_files=false;
 ufbx_error error{};std::unique_ptr<ufbx_scene,decltype(&ufbx_free_scene)> scene(ufbx_load_file(source.u8string().c_str(),&opts,&error),ufbx_free_scene);
 if(!scene){char message[2048];ufbx_format_error(message,sizeof(message),&error);throw std::runtime_error(message);}
 J meshes=J::array();std::map<size_t,size_t> totalCounts;size_t totalVertices=0,over4=0;
 for(auto* mesh:scene->meshes)for(auto* skin:mesh->skin_deformers){std::map<size_t,size_t> counts,positiveCounts,cornerCounts;std::vector<double> losses;double minSum=INFINITY,maxSum=0,maxLost=0;size_t negative=0,nonfinite=0,affected=0,dqNonzero=0,dqNonfinite=0,vertexIndex=0;double dqMin=INFINITY,dqMax=-INFINITY;J overFour=J::array();
  for(auto vertex:skin->vertices){double dq=vertex.dq_weight;if(std::isfinite(dq)){dqMin=std::min(dqMin,dq);dqMax=std::max(dqMax,dq);if(dq!=0)dqNonzero++;}else dqNonfinite++;if(vertex.num_weights>4){J values=J::array();for(size_t w=0;w<vertex.num_weights;w++){auto sw=skin->weights.data[vertex.weight_begin+w];values.push_back({{"clusterIndex",sw.cluster_index},{"weight",sw.weight}});}overFour.push_back({{"controlPoint",vertexIndex},{"dqWeight",dq},{"weights",values}});}vertexIndex++;counts[vertex.num_weights]++;totalCounts[vertex.num_weights]++;totalVertices++;std::vector<double> weights;double sum=0;for(size_t i=0;i<vertex.num_weights;i++){auto weight=skin->weights.data[vertex.weight_begin+i].weight;if(!std::isfinite(weight)){nonfinite++;continue;}if(weight<0)negative++;if(weight>0)weights.push_back(weight);sum+=weight;}positiveCounts[weights.size()]++;minSum=std::min(minSum,sum);maxSum=std::max(maxSum,sum);std::sort(weights.begin(),weights.end(),std::greater<double>());double tail=0;for(size_t i=4;i<weights.size();i++)tail+=weights[i];double fraction=sum>0?tail/sum:0;losses.push_back(fraction);maxLost=std::max(maxLost,fraction);if(weights.size()>4){affected++;over4++;}}
  std::vector<uint32_t> triangles(mesh->max_face_triangles*3);for(auto face:mesh->faces){auto count=ufbx_triangulate_face(triangles.data(),triangles.size(),mesh,face);for(size_t i=0;i<count*3;i++){auto index=mesh->vertex_indices.data[triangles[i]];cornerCounts[skin->vertices.data[index].num_weights]++;}}
  size_t rawNonzero=0,rawNonfinite=0,rawOutOfRange=0;double rawMin=INFINITY,rawMax=-INFINITY;for(auto weight:skin->dq_weights){if(std::isfinite(weight)){rawMin=std::min(rawMin,(double)weight);rawMax=std::max(rawMax,(double)weight);if(weight!=0)rawNonzero++;}else rawNonfinite++;}for(auto v:skin->dq_vertices)if(v>=mesh->num_vertices)rawOutOfRange++;J dqReport={{"perVertex",{{"count",skin->vertices.count},{"nonzero",dqNonzero},{"nonfinite",dqNonfinite},{"min",std::isfinite(dqMin)?dqMin:0},{"max",std::isfinite(dqMax)?dqMax:0}}},{"rawSparse",{{"weightCount",skin->dq_weights.count},{"vertexIndexCount",skin->dq_vertices.count},{"declaredCount",skin->num_dq_weights},{"nonzero",rawNonzero},{"nonfinite",rawNonfinite},{"outOfRangeVertices",rawOutOfRange},{"min",std::isfinite(rawMin)?rawMin:0},{"max",std::isfinite(rawMax)?rawMax:0}}},{"overFourVertices",overFour}};
  std::sort(losses.begin(),losses.end());auto percentile=[&](double p){return losses.empty()?0:losses[(size_t)std::floor((losses.size()-1)*p)];};
  meshes.push_back({{"dq",dqReport},{"name",std::string(mesh->name.data,mesh->name.length)},{"controlPoints",mesh->num_vertices},{"triangles",mesh->num_triangles},{"clusters",skin->clusters.count},{"skinningMethod",(int)skin->skinning_method},{"maxWeightsPerVertex",skin->max_weights_per_vertex},{"weightCountHistogram",histogram(counts)},{"positiveWeightHistogram",histogram(positiveCounts)},{"exportedTriangleCornerHistogram",histogram(cornerCounts)},{"positiveOverFourVertices",affected},{"weightSumMin",std::isfinite(minSum)?minSum:0},{"weightSumMax",maxSum},{"negativeWeights",negative},{"nonfiniteWeights",nonfinite},{"topFourLostFraction",{{"p50",percentile(.5)},{"p95",percentile(.95)},{"max",maxLost}}}});
 }
 J report={{"source",source.u8string()},{"meshCount",scene->meshes.count},{"nodeCount",scene->nodes.count},{"boneCount",scene->bones.count},{"skinControlPoints",totalVertices},{"positiveOverFourVertices",over4},{"weightCountHistogram",histogram(totalCounts)},{"meshes",meshes}};std::ofstream stream(output,std::ios::binary);stream<<report.dump(2);if(!stream)throw std::runtime_error("Cannot write measurement");std::cout<<J{{"skinControlPoints",totalVertices},{"positiveOverFourVertices",over4},{"weightCountHistogram",histogram(totalCounts)}}.dump()<<'\n';return 0;
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){try{if(argc!=3)throw std::runtime_error("inspect-skin source.fbx result.json");return inspect(argv[1],argv[2]);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
#else
int main(int argc,char** argv){try{if(argc!=3)throw std::runtime_error("inspect-skin source.fbx result.json");return inspect(argv[1],argv[2]);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
#endif
