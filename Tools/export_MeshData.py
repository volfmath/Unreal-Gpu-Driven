import unreal as ue
import codecs
import os
import time

def get_lastlod_triangles(comp):
    lodtriangles = ue.OriEditorFunctionLibrary.get_static_mesh_triangle(comp)
    if len(lodtriangles) > 0:
        return lodtriangles[-1]
    return 0


def get_instance_num(comp):
    NumInstances = comp.get_instance_count()
    return NumInstances

if __name__ == "__main__":
    csv_save_path = ue.Paths.project_saved_dir() + "MeshInfo/"
    if not os.path.exists(csv_save_path):
        os.makedirs(csv_save_path)
    level_name = ue.EditorLevelLibrary.get_editor_world().get_name()
    time_str = time.strftime("_%Y-%m-%d_%H.%M.%S", time.localtime())
    csv_name = csv_save_path + level_name + time_str + ".csv"
    csv_file = codecs.open(csv_name, "w", "utf-8")
    csv_file.write("Component, NumMesh, Type\n")
    comps = ue.EditorLevelLibrary.get_all_level_actors_components()
    
    InstanceMesh_name_list = []
    InstanceMesh_count_list = []
    InstanceMesh_type_list = []

    staticmesh_name_count_map = {}
    staticmesh_type_map = {}

    #last_lod_list = []
    
    for comp in comps:
        if ue.MathLibrary.class_is_child_of(comp.get_class(), ue.StaticMeshComponent):
            if comp.static_mesh is None:
                continue
            mesh_name = ue.SystemLibrary.get_display_name(comp.static_mesh)
            if ue.MathLibrary.class_is_child_of(comp.get_class(), ue.InstancedStaticMeshComponent):
                InstanceMesh_name_list.append(mesh_name)
                #last_lod_list.append(get_lastlod_triangles(comp))
                InstanceMesh_count_list.append(get_instance_num(comp))
                InstanceMesh_type_list.append(comp.get_class().get_name())
            else:
                if mesh_name not in staticmesh_name_count_map.keys():
                    staticmesh_name_count_map[mesh_name] = 1
                    staticmesh_type_map[mesh_name] = comp.get_class().get_name()
                else:
                    staticmesh_name_count_map[mesh_name] += 1


    for list_index in range(len(InstanceMesh_name_list)):
        csv_line = InstanceMesh_name_list[list_index] + "," + str(InstanceMesh_count_list[list_index]) + "," + InstanceMesh_type_list[list_index] + "\n"
        try:
            utf8_csv_line = csv_line.decode("utf-8")
            csv_file.write(utf8_csv_line)
        except UnicodeEncodeError:
            print("decode failed:" + csv_line)


    for staticmesh_name in staticmesh_name_count_map:
        csv_line = staticmesh_name + "," + str(staticmesh_name_count_map[staticmesh_name]) + "," + staticmesh_type_map[staticmesh_name] + "\n"
        try:
            utf8_csv_line = csv_line.decode("utf-8")
            csv_file.write(utf8_csv_line)
        except UnicodeEncodeError:
            print("decode failed:" + csv_line)

    csv_file.close()
    print("saved file: " + csv_name)

