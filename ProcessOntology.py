#!/usr/bin/env python
# coding: utf-8

from owlready2 import *
import re
import json
import sys

def remove_rdf_datatype(input_file, output_file=None):
    if output_file is None:
        output_file = input_file
    
    try:
        with open(input_file, 'r', encoding='utf-8') as file:
            content = file.read()
        
        modified_content = re.sub(r' rdf:datatype="&xml;string"', ' xml:lang="en"', content)
        
        with open(output_file, 'w', encoding='utf-8') as file:
            file.write(modified_content)
        
        print(f"File '{output_file}' has been processed successfully.")
    except Exception as e:
        print(f"An error occurred: {e}")

def clean_dictionary_content(content):
    cleaned_content = {}

    for line in content.split('\n'):
        if ':' in line:
            key, values_str = line.split(':', 1)
            key = key.strip().strip("'")

            values = re.findall(r"locstr\('([^']*)',\s*'[^']*'\)|'([^']*)'", values_str)

            # Clean the values by selecting the first valid match (either from locstr or direct string)
            cleaned_values = [clean_value(v[0] if v[0] else v[1]) for v in values]

            cleaned_content[key] = cleaned_values

    return cleaned_content
    
def clean_value(value):
    cleaned = re.sub(r'^[\d\-]+', '', value)
    return cleaned.strip()

pattern = re.compile(r'\d+ -')

def clean_string(s):
    s = re.sub(pattern, '', s)
    s = re.sub(r'[()]', '', s)
    s = s.replace(',', '')
    return s.strip()

# Function to remove unwanted patterns from strings in lists
def remove_patterns(d):
    cleaned_dict = {}
    for key, value in d.items():
        cleaned = (clean_string(value), d[key])
        cleaned_dict[key] = cleaned
    return cleaned_dict

def process_val(lst):
    res = set()
    for v in lst:
        res.add(str(v))
    return list(res)
    
def get_json(ontology, output_file):
    word_list = []
    # word_list = ['efsa', 'foodex2', 'food', 'product']
    onto = get_ontology(ontology).load()
    classes = {}
    exceptedClasses = set()
    i = 0
    for cls in onto.classes():
        # try:
        if cls.label:
            classes[cls.name] = cls.label
        else:
            exceptedClasses.add(cls)
        i += 1
        if i % 1000 == 0:
            print(f"this is the {i}th iter.")

    output = {}
    cleaned_classes = {}
    for k in classes:
        cleaned_classes[k] = classes[k][0]
    output = remove_patterns(cleaned_classes)
    for key, value in output.items():
        words = value[0].split(" ")
        filtered_words = [word for word in words if word not in word_list]
        output[key] = (" ".join(filtered_words), value[1])

    with open(output_file, "w") as file:
        json.dump(output, file)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print('Usage: python3 ./ProcessOntology.py [ontology file] [output file]')
        raise RuntimeError('Invalid input.')
    input_file = sys.argv[1]
    output_file = 'parsed_ontology.owl'
    remove_rdf_datatype(input_file, output_file)
    get_json(output_file, sys.argv[2])
