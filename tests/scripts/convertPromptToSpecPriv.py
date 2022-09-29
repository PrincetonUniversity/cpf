import os
import argparse


# Parse the rabbit6 file
def parse_rabbit6(fname):
    lines = []
    with open(fname, 'r') as f:
        lines = f.readlines()

    # Find all "Malloc ID #id : #name" into a dict
    malloc_id = {}
    for line in lines:
        if "Malloc ID" in line:
            line = line.replace("Malloc ID", "").replace(":", "").strip()
            mid, name = line.split(" ", 1)
            name = name.strip()
            # assert mid is an integer
            assert mid.isdigit()
            malloc_id[mid] = name

    # Find all "UO Inst #uoid : #name" into a dict
    # Or all "UO Arg #uoid : #name" 
    uo_id = {}
    for line in lines:
        if "UO Inst" in line or "UO Arg" in line:
            line = line.replace("UO Inst", "").replace("UO Arg", "").replace(":", "").strip()
            print(line)
            # split by the first space
            mid, name = line.split(" ", 1)
            name = name.strip()
            # assert mid is an integer
            assert mid.isdigit()
            uo_id[mid] = name


    # Find all "Function ID #fid: #name" into a dict
    func_id = {}
    for line in lines:
        if "Function ID" in line:
            line = line.replace("Function ID", "").replace(":", "").strip()
            mid, name = line.split(" ", 1)
            name = name.strip()
            # assert mid is an integer
            assert mid.isdigit()
            func_id[mid] = name

    # Find all "Loop ID #lid: #name" into a dict
    loop_id = {}
    for line in lines:
        if "Loop ID" in line:
            line = line.replace("Loop ID", "").replace(":", "").strip()
            mid, name = line.split(" ", 1)
            name = name.strip()
            # assert mid is an integer
            assert mid.isdigit()
            loop_id[mid] = name

    return malloc_id, uo_id, func_id, loop_id

def parse_specpriv(fname, id_maps):

    with open(fname, 'r') as f:
        lines = f.readlines()

    final_lines = []

    # For context, if the first number is 2, it's a loop context, get the name from loop_id map
    # if the first number is 1, it's a function context, get the name from func_id map
    # if the first number is 0, it's a Top context, the name is "TOP"
    def generateContextStr(contexts):
        # Get context from contexts and parse
        contexts = contexts.replace("]", "").strip().split(")(")

        context_str = ""
        # type, id
        for context in contexts:
            context = context.replace("(", "").replace(")", "").split(",")
            ctype, cid = context
            # assert ctype is an integer
            assert ctype.isdigit()
            # assert cid is an integer
            assert cid.isdigit()


            if ctype == "2":
                # loop context
                ctype_name = "LOOP"
                context_name = loop_ids[cid]
            elif ctype == "1":
                # function context
                ctype_name = "FUNCTION"
                context_name = func_ids[cid]
            elif ctype == "0":
                # top context
                ctype_name = "TOP"
                context_name = ""
            else:
                assert False

            context_str += ctype_name + " " + context_name
            if ctype != "0":
                context_str += " WITHIN "

        return context_str


    malloc_ids = id_maps['malloc']
    uo_ids = id_maps['uo']
    func_ids = id_maps['func']
    loop_ids = id_maps['loop']

    # Parse LOCAL OBJECT 279 at context (2,39)(2,35)(2,20)(1,10)(0,0);
    # Get malloc ID
    for line in lines:
        if "LOCAL OBJECT" in line:
            line = line.replace("LOCAL OBJECT", "").replace("at context", "").replace(";", "").strip()
            print(line)
            mid, contexts = line.split(" ", 1)

            # assert oid is an integer
            assert mid.isdigit()

            # Get the name of the malloc inst
            malloc_name = malloc_ids[mid]
            context_str = generateContextStr(contexts)

            final_lines.append("LOCAL OBJECT AU HEAP " +  malloc_name + " FROM CONTEXT { " + context_str + "} IS LOCAL TO CONTEXT { } COUNT 1")

    # Parse PRED OBJ 282: AU 24 FROM CONTEXT (1,0)(1,10)(0,0);
    # The first number is UO ID, the second number is malloc ID
    for line in lines:
        if "PRED OBJ" in line:
            line = line.replace("PRED OBJ", "").replace("AU ", "").replace("FROM CONTEXT", "").replace(":", "").replace(";", "").strip()
            if "NULL" in line:
                uoid, _ = line.split(" ", 1)
                # assert uoid is an integer
                assert uoid.isdigit()
                # Get the name of the malloc inst
                uo_name = uo_ids[uoid]

                final_lines.append("PRED OBJ " +  uo_name + " OVER 1 VALUES { ( OFFSET 0 BASE AU NULL COUNT 1 ) }")
            else:
                uoid, malloc_id, contexts = line.split(" ", 2)

                # assert uoid is an integer
                assert uoid.isdigit()
                assert malloc_id.isdigit()

                # Get the name of the malloc inst
                uo_name = uo_ids[uoid]
                malloc_name = malloc_ids[malloc_id]
                context_str = generateContextStr(contexts)

                final_lines.append("PRED OBJ " +  uo_name + " OVER 1 VALUES { ( OFFSET 0 BASE AU HEAP " + malloc_id + " FROM CONTEXT { " + context_str + "} COUNT 1 }")

    return final_lines


# Parse the rabbit6 file and the specpriv-profile.out file
def parse_args():
    parser = argparse.ArgumentParser(description='Parse the rabbit6 file and the specpriv-profile.out file')
    parser.add_argument('-r', '--rabbit6', help='Rabbit6 file', default="rabbit6", type=str)
    parser.add_argument('-p', '--profile', help='specpriv-profile.out file', default="specpriv-profile.out", type=str)
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()
    rabbit6 = args.rabbit6
    profile = args.profile

    # Parse the rabbit6 file
    malloc_id, uo_id, func_id, loop_id = parse_rabbit6(rabbit6)

    id_maps = {"malloc": malloc_id, "uo": uo_id, "func": func_id, "loop": loop_id}

    # Parse the specpriv-profile.out file
    final_lines = parse_specpriv(profile, id_maps)
    # print all lines
    for line in final_lines:
        print(line + ";")

