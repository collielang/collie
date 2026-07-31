---
sidebar_label: Container Type
---

import Tabs from '@theme/Tabs';
import TabItem from '@theme/TabItem';

# Container Type

## 🐳Type Overview {/* #intro */}

Data containers support dynamically adding, removing, modifying, and getting elements.

<Tabs>
  <TabItem value="mermaid" label="Mermaid" default>
    {/* https://mermaid.js.org/syntax/classDiagram.html#define-namespace */}
    ```mermaid
    classDiagram
    namespace Collections {
        class Collection

        class AbstractListCollection
        class AbstractSetCollection

        class list
        class set
    }

    namespace Dictionarys {
        class Dictionary
        class AbstractDictionary

        class map
    }

    object <|-- Collection
    object <|-- Dictionary

    Collection <|-- AbstractListCollection : implements
    Collection <|-- AbstractSetCollection : implements
    Dictionary <|-- AbstractDictionary : implements

    AbstractListCollection <|-- list : extends
    AbstractSetCollection <|-- set : extends
    AbstractDictionary <|-- map : extends

    <<interface>> Collection
    <<abstract>> AbstractListCollection
    <<abstract>> AbstractSetCollection
    <<abstract>> AbstractDictionary
    <<clazz>> list
    <<clazz>> set
    <<clazz>> map
    <<interface>> Dictionary
    object : equals()
    ```
  </TabItem>
  <TabItem value="plantuml" label="Plantuml (not support)">
    ```plantuml
    @startuml

    class object

    package "Collections" #EEEEEE {
        interface Collection
        object <|-- Collection

        abstract AbstractListCollection
        abstract AbstractSetCollection
        Collection <|-- AbstractListCollection
        Collection <|-- AbstractSetCollection

        class list
        class set

        AbstractListCollection <|-- list

        AbstractSetCollection <|-- set
        ' list <|-- set
    }

    package "Dictionarys" #EEEEEE {
        interface Dictionary
        object <|-- Dictionary

        abstract AbstractDictionary
        Dictionary <|-- AbstractDictionary

        class map

        AbstractDictionary <|-- map
    }

    object : equals()

    @enduml
    ```
  </TabItem>
</Tabs>
