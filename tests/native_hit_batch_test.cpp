#include "../common/native_hit_batch.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace bg3::native_hit;
struct Row {uint32_t words[64]{};};
int main(){
 batch::RowUploads<Row> uploads;uploads.finish();assert(uploads.runs.empty());
 // Reordered, sparse rows retain their exact destination; adjacent rows merge.
 Row a{},b{},c{};a.words[3]=11;b.words[7]=22;c.words[63]=33;
 uploads.add(16+512,c);uploads.add(16,a);uploads.add(16+256,b);uploads.add(16+1024,a);
 uploads.finish();assert(uploads.runs.size()==2);assert(uploads.runs[0].count==3);assert(uploads.runs[1].count==1);
 assert(uploads.runs[0].offset==16 && uploads.runs[1].offset==1040);
 assert(std::memcmp(&uploads.rows[0],&a,sizeof a)==0);assert(std::memcmp(&uploads.rows[1],&b,sizeof b)==0);
 assert(std::memcmp(&uploads.rows[2],&c,sizeof c)==0);
 // A long contiguous range is split at the Vulkan limit, not at an arbitrary
 // material count. Neither sparse holes nor a publication sentinel are written.
 batch::RowUploads<Row> long_rows;for(uint32_t i=0;i<257;++i){Row r{};r.words[0]=i;long_rows.add(32+uint64_t(i)*256,r);}
 long_rows.finish();assert(long_rows.runs.size()==2);assert(long_rows.runs[0].count==256);
 assert(long_rows.runs[1].offset==65568 && long_rows.runs[1].count==1);
 std::vector<unsigned char> memory(32+257*256+16,0xab);
 for(auto r:long_rows.runs)std::memcpy(memory.data()+r.offset,long_rows.rows.data()+r.first,r.count*sizeof(Row));
 for(size_t i=0;i<32;++i)assert(memory[i]==0xab);
 for(size_t i=memory.size()-16;i<memory.size();++i)assert(memory[i]==0xab);
 // Equal destinations preserve last-write order; finish is safe to repeat.
 uploads.add(16,b);uploads.finish();assert(uploads.runs.size()==3);
 assert(uploads.rows[0].words[3]==11 && uploads.rows[1].words[7]==22);
 uploads.finish();assert(uploads.runs.size()==3);
 std::vector<batch::JoinJob> jobs{{2,{10,11,12,13,14,15,16,17},4},{0,{20,21,22,23,24,25,26,27},5},{2,{30,31,32,33,34,35,36,37},6}};
 batch::group_chunks(jobs);assert(jobs[0].chunk==0 && jobs[0].push[7]==27 && jobs[0].groups==5);
 assert(jobs[1].chunk==2 && jobs[1].push[0]==10 && jobs[1].groups==4);
 assert(jobs[2].chunk==2 && jobs[2].push[0]==30 && jobs[2].groups==6);
 std::puts("native_hit_batch_test: sparse uploads, 64 KiB limit, sentinels and chunk jobs passed");
}
