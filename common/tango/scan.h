#ifndef TANGO_SCAN_H
#define TANGO_SCAN_H

#include <map>
#include <unordered_map>
#include <unordered_set>
#include "data/mesh.h"
#include "gl/glsl.h"
#include "tango/retango.h"
#include "tango/texturize.h"
#include "utils/union_find.h"

namespace oc {

    class TangoScan {
    public:
        TangoScan();
        ~TangoScan();
        void Add(GridIndex index, Tango3DR_Mesh* mesh) { meshes[index] = mesh; }
        std::vector<std::pair<GridIndex, Tango3DR_Mesh*> > Added() { return added; };
        void Clear();
        void ClearContext();
        void ClearGeometry();
        void ClearLast() { lastMerged.clear(); }
        std::vector<Component> Components() { return components; }
        Tango3DR_ReconstructionContext Context() { return context; }
        std::unordered_map<GridIndex, Tango3DR_Mesh*, GridIndexHasher> Data() { return meshes; }
        void Delete(std::vector<GridIndex>& toDelete);
        std::string DebugInfo();
        std::vector<Mesh> Export();
        std::vector<GridIndex> Last() { return lastMerged; }
        void Merge();
        void Merge(std::vector<std::pair<GridIndex, Tango3DR_Mesh *> >& data);
        void Reset3DR(double res, double dmin, double dmax, int noise, bool clearing = true);
        void Setup3DR(double res, double dmin, double dmax, int noise, bool clearing = true);
        float Resolution() { return (float)res_; }
        bool Update(Retango* depth, double timestamp, Tango3DR_Pose* t3dr_depth_pose,
                    Tango3DR_ImageBuffer* t3dr_image, Tango3DR_Pose* t3dr_image_pose,
                    bool postprocessing);
        
        // === PAUSE/RESUME FEATURE ===
        /**
         * Freeze current geometry - marks all current chunks as locked
         * Locked chunks won't be modified by further scanning
         * This allows scanning in segments with "checkpoints"
         */
        void FreezeGeometry();
        
        /**
         * Check if a chunk is frozen (locked)
         */
        bool IsFrozen(const GridIndex& index) const;
        
        /**
         * Get count of frozen chunks
         */
        int GetFrozenCount() const { return frozenChunks.size(); }
        
        /**
         * Unfreeze all geometry (for full rescan)
         */
        void UnfreezeAll() { frozenChunks.clear(); }
        // === END PAUSE/RESUME ===
        
    private:
        void GenerateComponents();
        void GenerateComponentsOptimized();
        void GenerateGraph();
        void GenerateGraphOptimized();
        void MergeComponents();
        void MergeComponentsOptimized();

        std::vector<std::pair<GridIndex, Tango3DR_Mesh*> > added;
        std::vector<Component> components;
        Tango3DR_ReconstructionContext context;
        std::vector<GridIndex> lastMerged;
        std::unordered_map<GridIndex, Tango3DR_Mesh*, GridIndexHasher> meshes;
        
        // Frozen (locked) chunks - won't be modified after freeze
        std::unordered_set<GridIndex, GridIndexHasher> frozenChunks;
        
        // Original string-based edge map (kept for compatibility)
        std::map<std::string, Edge> xorEdges;
        
        // Optimized integer-based edge map
        std::unordered_map<EdgeKey, Edge, EdgeKeyHash> xorEdgesOptimized;
        std::unordered_map<VertexKey, EdgeKey, VertexKeyHash> point2edgeOptimized;

        //setup
        bool clearing_;
        double res_;
        double dmin_;
        double dmax_;
        int noise_;

        //profiling
        int graph;
        int compo;
        int merge;
    };
}
#endif
