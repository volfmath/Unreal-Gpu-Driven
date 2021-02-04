import unreal as ue
import codecs
import os
import time

# todo: 当key相同时,

def get_lastlod_triangles(comp):
    lodtriangles = ue.OriEditorFunctionLibrary.get_static_mesh_triangle(comp)
    if len(lodtriangles) > 0:
        return lodtriangles[-1] #读取倒数第一个元素 https://blog.csdn.net/HARDBIRD123/article/details/82261651
    return 0

def get_component_instance_num(comp):
    if ue.MathLibrary.class_is_child_of(comp.get_class(), ue.InstancedStaticMeshComponent):
        return comp.get_instance_count()
    return 1

def rename_actor(actor, str):
    str = str.replace("SM_LSC_Rock01", "SM_BeachRockA")
    print(str)
    ue.OriEditorFunctionLibrary.rename_uobject(actor, str)


if __name__ == "__main__":
    csv_save_path = ue.Paths.project_saved_dir() + "MeshInfo/"
    if not os.path.exists(csv_save_path):
        os.makedirs(csv_save_path)
    level_name = ue.EditorLevelLibrary.get_editor_world().get_name()
    time_str = time.strftime("_%Y-%m-%d_%H.%M.%S", time.localtime())
    csv_name = csv_save_path + level_name + time_str + ".csv"
    csv_file = codecs.open(csv_name, "w", "utf-8")
    csv_file.write("Component, NumMesh, ComponentCount \n")
    comps = ue.EditorLevelLibrary.get_all_level_actors_components()

    key_meshname_map = {}
    key_meshcount_map = {}
    key_componentcount_map = {}

    #last_lod_list = []
    
    for comp in comps:
        if ue.MathLibrary.class_is_child_of(comp.get_class(), ue.StaticMeshComponent):
            if comp.static_mesh is None:
                continue

            materials = ue.OriEditorFunctionLibrary.get_primitive_component_used_materials(comp)
            mesh_name = ue.SystemLibrary.get_display_name(comp.static_mesh)
            key = mesh_name
            # key = mesh_name+ comp.get_class().get_name()
            if len(materials) > 0:
                key = key + ue.SystemLibrary.get_display_name(materials[0])

            if comp.get_owner() is not None:
                actor_name = ue.SystemLibrary.get_display_name(comp.get_owner())
                if "SM_LSC_Rock01" in actor_name:
                    rename_actor(comp.get_owner(), actor_name)


            if key not in key_meshname_map:
                key_meshname_map[key] = mesh_name
                key_meshcount_map[key] = get_component_instance_num(comp)
                key_componentcount_map[key] = 1
            else:
                key_meshcount_map[key] += get_component_instance_num(comp)
                key_componentcount_map[key] += 1
            

    

    for key in key_meshname_map:
        csv_line = key_meshname_map[key] + "," + str(key_meshcount_map[key]) + "," + str(key_componentcount_map[key]) + "\n"
        try:
            utf8_csv_line = csv_line.decode("utf-8")
            csv_file.write(utf8_csv_line)
        except UnicodeEncodeError:
            print("decode failed:" + csv_line)

    csv_file.close()
    print("saved file: " + csv_name)

